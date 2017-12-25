/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <ev.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <deai/deai.h>
#include <deai/helper.h>

#include <config.h>

#include "di_internal.h"
#include "env.h"
#include "event.h"
#include "log.h"
#include "spawn.h"
#include "uthash.h"
#include "utils.h"

static void load_plugin(struct deai *p, const char *sopath) {
	if (!sopath)
		return;

	void *handle = dlopen(sopath, RTLD_NOW);

	if (!handle) {
		fprintf(stderr, "Failed to load %s: %s\n", sopath, dlerror());
		return;
	}

	init_fn_t init_fn = dlsym(handle, "di_plugin_init");
	if (!init_fn) {
		fprintf(stderr, "%s doesn't have a di_plugin_init function\n", sopath);
		return;
	}

	init_fn(p);
}

static int load_plugin_dir(struct deai *di, const char *path) {
	if (!path)
		return -1;

	char rpath[PATH_MAX];
	realpath(path, rpath);

	int dirfd = open(path, O_DIRECTORY | O_RDONLY);
	if (dirfd < 0)
		return -1;

	DIR *dir = fdopendir(dirfd);
	if (!dir) {
		close(dirfd);
		return -1;
	}

	struct dirent *dent;
	while ((dent = readdir(dir))) {
		bool is_reg = dent->d_type == DT_REG;
		if (dent->d_type == DT_UNKNOWN || dent->d_type == DT_LNK) {
			struct stat buf;
			int ret = fstatat(dirfd, dent->d_name, &buf, 0);
			if (ret != 0) {
				perror("stat");
				return -1;
			}

			is_reg = S_ISREG(buf.st_mode);
		}

		if (!is_reg)
			continue;

		int nlen = strlen(dent->d_name);
		if (nlen >= 3 && strcmp(dent->d_name + nlen - 3, ".so") == 0) {
			char *sopath;
			asprintf(&sopath, "%s/%s", rpath, dent->d_name);
			load_plugin(di, sopath);
			free(sopath);
		}
	}
	closedir(dir);
	return 0;
}

int di_chdir(struct di_object *p, const char *dir) {
	if (!dir)
		return -EINVAL;

	int ret = chdir(dir);
	if (ret != 0)
		ret = -errno;

	errno = 0;
	return ret;
}

void di_dtor(struct deai *di) {
	*di->quit = true;
#ifdef HAVE_SETPROCTITLE
	for (int i = 0; i < di->argc; i++)
		free(di->argv[i]);
	free(di->argv);
#endif

	// fprintf(stderr, "%d\n", p->ref_count);
	for (int i = 0; di->env_copy[i]; i++)
		free(di->env_copy[i]);
	free(di->env_copy);
	ev_break(di->loop, EVBREAK_ALL);
}

struct di_ev_prepare {
	ev_prepare;
	struct deai *di;
};

void di_quit_cb(EV_P_ ev_prepare *w, int revents) {
	struct di_ev_prepare *evp = (void *)w;
	di_apoptosis((void *)evp->di);
}

void di_prepare_quit(struct deai *di) {
	auto evp = tmalloc(struct di_ev_prepare, 1);
	evp->di = di;
	ev_prepare_init(evp, di_quit_cb);
	ev_prepare_start(di->loop, (void *)evp);
}

struct di_ev_signal {
	ev_signal;
	void *ud;
	int sig;
};

static void di_sighandler(struct ev_loop *l, ev_signal *w, int revents) {
	ev_break(l, EVBREAK_ALL);
}

#ifdef HAVE_SETPROCTITLE
static void setproctitle_init(int argc, char **argv, struct deai *p) {
	// Copy argv and environ
	p->argc = argc;
	p->argv = calloc(argc, sizeof(void *));
	for (int i = 0; i < argc; i++) {
		// fprintf(stderr, "%p\n", argv[i]);
		p->argv[i] = strdup(argv[i]);
	}

	p->proctitle = argv[0];

	size_t envsz = 0;
	for (; environ[envsz]; envsz++)
		;
	char **old_env = environ;
	environ = calloc(envsz + 1, sizeof(char *));
	p->env_copy = calloc(envsz + 1, sizeof(char *));
	for (int i = 0; i < envsz; i++) {
		// fprintf(stderr, "%p %s %p\n", old_env[i], old_env[i],
		//        old_env[i] + strlen(old_env[i]));
		environ[i] = strdup(old_env[i]);
		p->env_copy[i] = environ[i];
	}
	environ[envsz] = NULL;
	p->env_copy[envsz] = NULL;

	// Available space extends until the end of the page
	uintptr_t end = (uintptr_t)p->proctitle;
	auto pgsz = getpagesize();
	end = (end / pgsz + 1) * pgsz;
	p->proctitle_end = (void *)end;
	// fprintf(stderr, "%p, %lu\n", p->proctitle, p->proctitle_end -
	// p->proctitle);
}
static void setproctitle_deinit(void) {
}
static void di_set_pr_name(struct deai *p, const char *name) {
	if (name) {
		memset(p->proctitle, 0, p->proctitle_end - p->proctitle);
		auto nlen = strlen(name);

		if (p->proctitle + nlen + 1 >= p->proctitle_end)
			nlen = p->proctitle_end - p->proctitle - 1;
		strncpy(p->proctitle, name, nlen);
	}
}
#else
// no-op
static void setproctitle_init(int argc, struct deai *p) {
}
static void di_set_pr_name(struct deai *p, const char *name) {
}
static void setproctitle_deinit(void) {
}
#endif

static struct di_array di_get_argv(struct deai *p) {
	struct di_array ret;
	ret.length = p->argc;
	ret.elem_type = DI_TYPE_STRING;
	ret.arr = calloc(p->argc, sizeof(void *));

	const char **arr = ret.arr;
	for (int i = 0; i < p->argc; i++)
		arr[i] = strdup(p->argv[i]);

	return ret;
}

PUBLIC int di_register_module(struct deai *p, const char *name, struct di_module *m) {
	return di_add_value_member((void *)p, name, false, DI_TYPE_OBJECT, m);
}

define_trivial_cleanup(char *, free_charpp);

int di_exec(struct deai *p, struct di_array argv) {
	with_cleanup(free_charpp) char **nargv = tmalloc(char *, argv.length + 1);
	memcpy(nargv, argv.arr, sizeof(void *) * argv.length);

	execvp(nargv[0], nargv);
	return -1;
}

// Terminate self and all children.
//
// This is a best effort attempt, some of the children might have moved to different
// process group.
void di_terminate(struct deai *p) {
	kill(0, SIGTERM);
}

int main(int argc, char *argv[]) {
	struct deai *p = di_new_object_with_type(struct deai);
	p->loop = EV_DEFAULT;

	// We want to be our own process group leader
	setpgid(0, 0);

	// (1) Initialize builtin modules first
	di_init_event(p);
	di_init_log(p);
	di_init_env(p);
	di_init_spawn(p);

	if (argc < 2) {
		printf("Usage: %s <module>.<method> <arg1> <arg2> ...\n", argv[0]);
		exit(1);
	}

	di_method(p, "load_plugin_from_dir", load_plugin_dir, char *);
	di_method(p, "load_plugin", load_plugin, char *);
	di_method(p, "register_module", di_register_module, char *,
	          struct di_object *);
	di_method(p, "chdir", di_chdir, char *);
	di_method(p, "exec", di_exec, struct di_array);

	/**
	 * XXX: Quit method should delay the __destroyed signal until control flow
	 * gets back to the mainloop. The destruction shouldn't happen on someone
	 * else's stack frame
	 */
	di_method(p, "quit", di_prepare_quit);
	di_method(p, "terminate", di_terminate);
	di_method(p, "__set_proctitle", di_set_pr_name, char *);
	di_method(p, "__get_argv", di_get_argv);

	di_add_ref_member((void *)p, "proctitle", false, DI_TYPE_STRING_LITERAL,
	                  &p->proctitle);

	struct di_ev_signal sigintw;
	sigintw.ud = p;
	sigintw.sig = SIGINT;
	ev_signal_init(&sigintw, di_sighandler, SIGINT);
	ev_signal_start(p->loop, (void *)&sigintw);

	struct di_ev_signal sigtermw;
	sigtermw.ud = p;
	sigtermw.sig = SIGTERM;
	ev_signal_init(&sigtermw, di_sighandler, SIGTERM);
	ev_signal_start(p->loop, (void *)&sigtermw);

	// (2) Parse commandline
	char *method = strchr(argv[1], '.');
	if (!method) {
		fprintf(stderr, "Malformed module.method name\n");
		exit(EXIT_FAILURE);
	}

	auto modname = strndup(argv[1], method - argv[1]);

	method = strdup(method + 1);

	void **di_args = calloc(argc - 2, sizeof(void *));
	di_type_t *di_types = calloc(argc - 2, sizeof(di_type_t));
	int nargs = 0;
	for (int i = 2; i < argc; i++) {
		if (strcmp(argv[i], "--") == 0)
			break;
		if (argv[i][1] != ':') {
			fprintf(stderr, "Invalid argument: %s\n", argv[i]);
			exit(EXIT_FAILURE);
			return 1;
		}
		switch (argv[i][0]) {
		case 'i':        // Integer
			di_args[nargs] = malloc(sizeof(int64_t));
			di_types[nargs] = DI_TYPE_INT;
			*(int64_t *)di_args[nargs] = atoll(argv[i] + 2);
			break;
		case 's':        // String
			di_args[nargs] = malloc(sizeof(const char *));
			di_types[nargs] = DI_TYPE_STRING;
			*(const char **)di_args[nargs] = argv[i] + 2;
			break;
		case 'f':        // Float
			di_types[nargs] = DI_TYPE_FLOAT;
			di_args[nargs] = malloc(sizeof(double));
			*(double *)di_args[nargs] = atof(argv[i] + 2);
			break;
		default:
			fprintf(stderr, "Invalid argument type: %s\n", argv[i]);
			exit(EXIT_FAILURE);
		}
		nargs++;
	}

	setproctitle_init(argc, argv, p);

	// (3) Load default plugins
	int ret = load_plugin_dir(p, DI_PLUGIN_INSTALL_DIR);
	if (ret != 0) {
		fprintf(stderr, "Failed to load plugins\n");
		exit(EXIT_FAILURE);
	}

	struct di_object *mod;
	ret = di_get(p, modname, mod);
	if (ret != 0) {
		fprintf(stderr, "Module \"%s\" not found\n", modname);
		exit(EXIT_FAILURE);
	}

	di_type_t rt;
	void *retd = NULL;
	ret = di_rawcallxn(mod, method, &rt, &retd,
	                   (struct di_tuple){nargs, di_args, di_types});
	if (ret != 0) {
		fprintf(stderr, "Failed to call \"%s.%s\"\n", modname, method);
		exit(EXIT_FAILURE);
	}

	di_free_value(rt, retd);
	free(retd);
	free(method);
	free(modname);

	for (int i = 0; i < nargs; i++)
		free((void *)di_args[i]);
	free(di_args);
	free(di_types);
	di_unref_object(mod);

	// (4) Start mainloop
	bool quit = false;
	p->dtor = (void *)di_dtor;
	p->quit = &quit;
	di_unref_object((void *)p);
	if (!quit)
		ev_run(p->loop, 0);

	setproctitle_deinit();
	return 0;
}
