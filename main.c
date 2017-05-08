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

#include <deai.h>

#include "config.h"
#include "di_internal.h"
#include "env.h"
#include "event_internal.h"
#include "log_internal.h"
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
	DIR *dir = fdopendir(dirfd);
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

void di_quit(struct deai *di) {
	// This function can be called before ev_run
	// If so, don't even call ev_run
	di->quit = true;
	ev_break(di->loop, EVBREAK_ALL);
}

struct di_ev_signal {
	ev_signal;
	struct deai *di;
	int sig;
};

static void di_sighandler(struct ev_loop *l, ev_signal *w, int revents) {
	ev_break(l, EVBREAK_ALL);
}

static char *di_get_pr_name(struct deai *p) {
	return strdup(p->argv[0]);
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
	environ = calloc(envsz + 1, sizeof(void *));
	for (int i = 0; i < envsz; i++) {
		// fprintf(stderr, "%p %s %p\n", old_env[i], old_env[i],
		//        old_env[i] + strlen(old_env[i]));
		environ[i] = strdup(old_env[i]);
	}
	environ[envsz] = NULL;

	// Available space extends until the end of the page
	uintptr_t end = (uintptr_t)p->proctitle;
	auto pgsz = getpagesize();
	end = (end / pgsz + 1) * pgsz;
	p->proctitle_end = (void *)end;
	// fprintf(stderr, "%p, %lu\n", p->proctitle, p->proctitle_end -
	// p->proctitle);
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

int main(int argc, char *argv[]) {
	struct di_module_internal *pm;
	struct deai *p = di_new_object_with_type(struct deai);
	p->m = NULL;
	p->loop = EV_DEFAULT;

	// Signal for when new module is added
	di_register_signal((void *)p, "new-module", 1, DI_TYPE_STRING);
	// Signal for when deai is shutting down
	di_register_signal((void *)p, "shutdown", 0);

	// (1) Initialize builtin modules first
	di_init_event_module(p);
	di_init_log(p);
	di_init_env(p);

	if (argc < 2) {
		printf("Usage: %s <module>.<method> <arg1> <arg2> ...\n", argv[0]);
		exit(1);
	}

	di_register_typed_method(
	    (void *)p,
	    di_create_typed_method((di_fn_t)load_plugin_dir, "load_plugin_from_dir",
	                           DI_TYPE_NINT, 1, DI_TYPE_STRING));

	di_register_typed_method(
	    (void *)p, di_create_typed_method((di_fn_t)load_plugin, "load_plugin",
	                                      DI_TYPE_NINT, 1, DI_TYPE_STRING));

	di_register_typed_method(
	    (void *)p, di_create_typed_method((di_fn_t)di_chdir, "chdir",
	                                      DI_TYPE_NINT, 1, DI_TYPE_STRING));

	di_register_typed_method(
	    (void *)p,
	    di_create_typed_method((di_fn_t)di_quit, "quit", DI_TYPE_VOID, 0));

	di_register_typed_method(
	    (void *)p, di_create_typed_method((di_fn_t)di_get_pr_name,
	                                      "__get_proctitle", DI_TYPE_STRING, 0));

	di_register_typed_method(
	    (void *)p,
	    di_create_typed_method((di_fn_t)di_set_pr_name, "__set_proctitle",
	                           DI_TYPE_VOID, 1, DI_TYPE_STRING));

	di_register_typed_method(
	    (void *)p, di_create_typed_method((di_fn_t)di_get_argv, "__get_argv",
	                                      DI_TYPE_ARRAY, 0));

	di_register_typed_method(
	    (void *)p, di_create_typed_method((di_fn_t)di_find_module, "__get",
	                                      DI_TYPE_OBJECT, 1, DI_TYPE_STRING));

	struct di_ev_signal sigintw;
	sigintw.di = p;
	sigintw.sig = SIGINT;
	ev_signal_init(&sigintw, di_sighandler, SIGINT);
	ev_signal_start(p->loop, (void *)&sigintw);

	struct di_ev_signal sigtermw;
	sigtermw.di = p;
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

	const void **di_args = calloc(argc - 2, sizeof(void *));
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

	auto mod = di_find_module(p, modname);
	if (!mod) {
		fprintf(stderr, "Module \"%s\" not found\n", modname);
		exit(EXIT_FAILURE);
	}

	auto mt = di_find_method((void *)mod, method);
	if (!mt) {
		fprintf(stderr, "Method \"%s\" not found in module \"%s\"\n", method,
		        modname);
		exit(EXIT_FAILURE);
	}
	free(method);
	free(modname);

	di_type_t rtype;
	void *retv;
	ret =
	    di_call_callable((void *)mt, &rtype, &retv, nargs, di_types, di_args);
	if (ret != 0) {
		fprintf(stderr, "Failed to call init function\n");
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < nargs; i++)
		free((void *)di_args[i]);
	free(di_args);
	free(di_types);
	di_unref_object((void *)mod);

	// (4) Start mainloop
	if (!p->quit)
		ev_run(p->loop, 0);

	if (rtype == DI_TYPE_OBJECT && *(void **)retv)
		di_unref_object(*(struct di_object **)retv);
	free(retv);

	// (5) Exit
	di_emit_signal_v((void *)p, "shutdown");
	pm = p->m;
	while (pm) {
		auto next_pm = pm->hh.next;
		HASH_DEL(p->m, pm);
		// printf("%s:%d\n",pm->name, pm->ref_count);
		di_unref_object((void *)pm);
		pm = next_pm;
	}

	for (int i = 0; i < p->argc; i++)
		free(p->argv[i]);
	free(p->argv);
	di_unref_object((void *)p);
}
