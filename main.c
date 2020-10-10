/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <ev.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef __FreeBSD__
#include <sys/procctl.h>
#endif
#include <unistd.h>

#include <deai/builtin/log.h>
#include <deai/deai.h>
#include <deai/helper.h>

#include <config.h>

#include "di_internal.h"
#include "event.h"
#include "log.h"
#include "os.h"
#include "spawn.h"
#include "uthash.h"
#include "utils.h"

static void load_plugin(struct deai *p, const char *sopath) {
	if (!sopath) {
		return;
	}

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
	if (!path) {
		return -1;
	}

	char rpath[PATH_MAX];
	realpath(path, rpath);

	int dirfd = open(path, O_DIRECTORY | O_RDONLY);
	if (dirfd < 0) {
		return -1;
	}

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

		if (!is_reg) {
			continue;
		}

		size_t nlen = strlen(dent->d_name);
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
	if (!dir) {
		return -EINVAL;
	}

	int ret = chdir(dir);
	if (ret != 0) {
		ret = -errno;
	}

	errno = 0;
	return ret;
}

#ifdef __FreeBSD__
static void kill_all_descendants(void) {
	struct procctl_reaper_status status;
	int ret = procctl(P_PID, getpid(), PROC_REAP_STATUS, &status);
	if (ret != 0) {
		fprintf(stderr, "Failed to get reap status (%s), giving up\n", strerror(errno));
		return;
	}
	if (status.rs_descendants == 0) {
		// Nothing needs to be killed
		return;
	}
	struct procctl_reaper_kill k = {
	    .rk_sig = SIGKILL,
	    .rk_flags = 0,
	    .rk_subtree = 0,
	};
	ret = procctl(P_PID, getpid(), PROC_REAP_KILL, &k);
	if (ret != 0) {
		fprintf(stderr, "Failed to reap children %s\n", strerror(errno));
	}
	return;
}
#else
// Consider PRDEATHSIG
static void kill_all_descendants(void) {
	// Best effort attempt to kill all descendants of ourself
	struct _childp {
		pid_t pid, ppid;
		bool visited;
		struct _childp *pp;
		struct list_head ll;
		UT_hash_handle hh;
	};
	pid_t pid = getpid();

	struct _childp *ps = NULL;
	struct _childp *self = NULL;

	// New child can appear while we are reading /proc
	// they won't be killed
	auto dir = opendir("/proc");
	if (!dir) {
		return;
	}
	struct dirent *dent;
	char pathbuf[PATH_MAX];
	char textbuf[4096];
	while ((dent = readdir(dir))) {
		__label__ next;
		if (dent->d_type != DT_DIR) {
			continue;
		}
		char *name = dent->d_name;
		while (*name) {
			if (!isdigit(*name++)) {
				goto next;
			}
		}
		snprintf(pathbuf, PATH_MAX, "/proc/%s/stat", dent->d_name);
		pid_t cpid = atoi(dent->d_name);
		int fd = open(pathbuf, O_RDONLY);
		if (fd < 0) {
			continue;
		}
		ssize_t ret;
		size_t cap = 0;
		char *stattext = NULL;
		while ((ret = read(fd, textbuf, sizeof(textbuf))) > 0) {
			stattext = realloc(stattext, cap + ret + 1);
			memcpy(stattext + cap, textbuf, ret);
			cap += ret;
		}
		close(fd);
		if (!stattext) {
			continue;
		}
		stattext[cap] = '\0';

		// End of comm
		char *sep1 = strrchr(stattext, ')');
		// Skip ') %c ', and find the end of ppid
		char *ppid_end = strchr(sep1 + 4, ' ');
		*ppid_end = '\0';
		pid_t ppid = atoi(sep1 + 4);
		free(stattext);

		auto np = tmalloc(struct _childp, 1);
		np->pid = cpid;
		np->ppid = ppid;
		INIT_LIST_HEAD(&np->ll);
		HASH_ADD_INT(ps, pid, np);
		if (cpid == pid) {
			self = np;
		}
	next:;
	}
	closedir(dir);

	// Link the process tree into pre-order traversal list
	struct _childp *i, *ni;
	struct _childp sentinel;
	INIT_LIST_HEAD(&sentinel.ll);
	HASH_ITER (hh, ps, i, ni) {
		struct _childp *pp;
		HASH_FIND_INT(ps, &i->ppid, pp);
		if (!pp) {
			pp = &sentinel;
		}
		i->pp = pp;
		__list_splice(&i->ll, &pp->ll, pp->ll.next);
	}

	// Traversal the tree, starting from ourself
	self->visited = true;
	sentinel.visited = false;
	struct _childp *curr = self;
	while (1) {
		curr = container_of(curr->ll.next, struct _childp, ll);
		// We got out of our subtree
		if (!curr->pp->visited) {
			break;
		}
		kill(curr->pid, SIGTERM);
		curr->visited = true;
	}
	HASH_ITER (hh, ps, i, ni) {
		HASH_DEL(ps, i);
		free(i);
	}
}
#endif

void di_dtor(struct deai *di) {
	*di->quit = true;

#ifdef HAVE_SETPROCTITLE
	for (int i = 0; i < di->argc; i++) {
		free(di->argv[i]);
	}
	free(di->argv);
#endif

	// fprintf(stderr, "%d\n", p->ref_count);
	kill_all_descendants();
	ev_break(di->loop, EVBREAK_ALL);
}

struct di_ev_prepare {
	ev_prepare;
	struct deai *di;
};

void di_prepare_quit(struct deai *di) {
	di_schedule_call(di, di_destroy_object, ((struct di_object *)di));
}

void di_prepare_exit(struct deai *di, int ec) {
	*di->exit_code = ec;
	di_schedule_call(di, di_destroy_object, ((struct di_object *)di));
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
	for (; environ[envsz]; envsz++) {
	}

	char **old_env = environ;
	environ = calloc(envsz + 1, sizeof(char *));
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

		if (p->proctitle + nlen + 1 >= p->proctitle_end) {
			nlen = p->proctitle_end - p->proctitle - 1;
		}
		strncpy(p->proctitle, name, nlen);
	}
}
#else
// no-op
static void setproctitle_init(int argc, char **argv, struct deai *p) {
	p->argc = argc;
	p->argv = argv;
}
#endif

static struct di_array di_get_argv(struct deai *p) {
	struct di_array ret;
	ret.length = p->argc;
	ret.elem_type = DI_TYPE_STRING;
	ret.arr = calloc(p->argc, sizeof(void *));

	const char **arr = ret.arr;
	for (int i = 0; i < p->argc; i++) {
		arr[i] = strdup(p->argv[i]);
	}

	return ret;
}

PUBLIC int di_register_module(struct deai *p, const char *name, struct di_module **m) {
	int ret =
	    di_add_member_move((void *)p, name, (di_type_t[]){DI_TYPE_OBJECT}, (void **)m);
	return ret;
}

// Don't consumer the ref, because it breaks the usual method call sementics
static int di_register_module_method(struct deai *p, const char *name, struct di_module *m) {
	return di_add_member_clone((void *)p, name, DI_TYPE_OBJECT, m);
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

/// Add an object as a root to keep it alive
void di_add_root(struct di_object *di, const char *key, struct di_object *obj) {
	di_getmi(di, log);

	char *buf;
	asprintf(&buf, "__root_%s", key);
	int rc = di_add_member_clone(di, buf, DI_TYPE_OBJECT, obj);
	if (rc != 0) {
		di_log_va(logm, DI_LOG_ERROR, "cannot add root\n");
	}
	free(buf);
}

/// Remove an object from roots
void di_remove_root(struct di_object *di, const char *key) {
	di_getmi(di, log);

	char *buf;
	asprintf(&buf, "__root_%s", key);
	int rc = di_remove_member_raw(di, buf);
	if (rc != 0) {
		di_log_va(logm, DI_LOG_ERROR, "cannot remove root\n");
	}
	free(buf);
}

/// Remove all roots
void di_clear_roots(struct di_object *di_) {
	static const char *const root_prefix = "__root_";
	auto di = (struct di_object_internal *)di_;
	struct di_member *i, *tmp;
	HASH_ITER (hh, di->members, i, tmp) {
		if (strncmp(i->name, root_prefix, strlen(root_prefix)) == 0) {
			di_remove_member_raw(di_, i->name);
		}
	}
}

int main(int argc, char *argv[]) {
#ifdef TRACK_OBJECTS
	INIT_LIST_HEAD(&all_objects);
#endif
	struct deai *p = di_new_object_with_type(struct deai);
	int exit_code = 0;
	bool quit = false;
	p->loop = EV_DEFAULT;
	p->exit_code = &exit_code;
	p->quit = &quit;
	p->dtor = (void *)di_dtor;

	// We want to be our own process group leader
	setpgid(0, 0);

	// (1) Initialize builtin modules first
	di_init_event(p);
	di_init_log(p);
	di_init_os(p);
	di_init_spawn(p);

	if (argc < 2) {
		printf("Usage: %s <module>.<method> <arg1> <arg2> ...\n", argv[0]);
		exit(1);
	}

	DI_CHECK_OK(di_method(p, "load_plugin_from_dir", load_plugin_dir, char *));
	DI_CHECK_OK(di_method(p, "load_plugin", load_plugin, char *));
	DI_CHECK_OK(di_method(p, "register_module", di_register_module_method, char *,
	                      struct di_object *));
	DI_CHECK_OK(di_method(p, "add_root", di_add_root, const char *, struct di_object *));
	DI_CHECK_OK(di_method(p, "remove_root", di_remove_root, const char *));
	DI_CHECK_OK(di_method(p, "clear_roots", di_clear_roots));
	DI_CHECK_OK(di_method(p, "chdir", di_chdir, char *));
	DI_CHECK_OK(di_method(p, "exec", di_exec, struct di_array));

	DI_CHECK_OK(di_method(p, "quit", di_prepare_quit));
	DI_CHECK_OK(di_method(p, "exit", di_prepare_exit, int));
	DI_CHECK_OK(di_method(p, "terminate", di_terminate));
#ifdef HAVE_SETPROCTITLE
	DI_CHECK_OK(di_method(p, "__set_proctitle", di_set_pr_name, char *));
#endif
	DI_CHECK_OK(di_method(p, "__get_argv", di_get_argv));

	DI_CHECK_OK(di_add_member_ref((void *)p, "proctitle", DI_TYPE_STRING_LITERAL,
	                              &p->proctitle));

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
	char *modname = NULL;
	char *method = strchr(argv[1], '.');
	if (method) {
		modname = strndup(argv[1], method - argv[1]);
		method = strdup(method + 1);
	} else {
		method = strdup(argv[1]);
	}

	auto di_args = tmalloc(struct di_variant, argc - 2);
	int nargs = 0;
	for (int i = 2; i < argc; i++) {
		if (strcmp(argv[i], "--") == 0) {
			break;
		}
		if (argv[i][1] != ':') {
			fprintf(stderr, "Invalid argument: %s\n", argv[i]);
			exit(EXIT_FAILURE);
			return 1;
		}
		switch (argv[i][0]) {
		case 'i':        // Integer
			di_args[nargs].value = malloc(sizeof(int64_t));
			di_args[nargs].type = DI_TYPE_INT;
			di_args[nargs].value->int_ = atoll(argv[i] + 2);
			break;
		case 's':        // String
			di_args[nargs].value = malloc(sizeof(const char *));
			di_args[nargs].type = DI_TYPE_STRING_LITERAL;
			di_args[nargs].value->string_literal = argv[i] + 2;
			break;
		case 'f':        // Float
			di_args[nargs].value = malloc(sizeof(double));
			di_args[nargs].type = DI_TYPE_FLOAT;
			di_args[nargs].value->float_ = atof(argv[i] + 2);
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
		fprintf(stderr, "Failed to load plugins from \"%s\", which is fine.\n",
		        DI_PLUGIN_INSTALL_DIR);
	}

	struct di_object *mod = NULL;
	if (modname) {
		ret = di_get(p, modname, mod);
		if (ret != 0) {
			fprintf(stderr, "Module \"%s\" not found\n", modname);
			exit(EXIT_FAILURE);
		}
	} else {
		mod = di_ref_object((struct di_object *)p);
	}

	di_type_t rt;
	union di_value retd;
	bool called;
	ret = di_rawcallxn(mod, method, &rt, &retd, (struct di_tuple){nargs, di_args}, &called);
	if (ret != 0) {
		fprintf(stderr, "Failed to call \"%s.%s\"\n", modname ? modname : "", method);
		exit(EXIT_FAILURE);
	}

	di_free_value(rt, &retd);
	free(method);
	free(modname);

	for (int i = 0; i < nargs; i++) {
		di_free_value(DI_TYPE_VARIANT, (union di_value *)&di_args[i]);
	}
	free(di_args);

	di_unref_object(mod);

	// (4) Start mainloop
	di_unref_object((void *)p);

#ifdef TRACK_OBJECTS
	di_dump_objects();
#endif
	if (!quit) {
		ev_run(p->loop, 0);
	}

	return exit_code;
}
