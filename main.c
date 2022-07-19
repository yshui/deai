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

#include <deai/builtins/log.h>
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

static void load_plugin_impl(struct deai *p, char *sopath) {

	void *handle = dlopen(sopath, RTLD_NOW | RTLD_LOCAL);

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

/// Load a single plugin
///
/// EXPORT: load_plugin(file: :string): :void
static void load_plugin(struct deai *p, struct di_string sopath) {
	if (!sopath.data) {
		return;
	}

	with_cleanup_t(char) sopath_str = di_string_to_chars_alloc(sopath);
	load_plugin_impl(p, sopath_str);
}

static int load_plugin_from_dir_impl(struct deai *di, const char *path) {

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
			load_plugin_impl(di, sopath);
			free(sopath);
		}
	}
	closedir(dir);
	return 0;
}

/// Load plugins from a directory
///
/// EXPORT: load_plugin_from_dir(path: :string): :integer
static int load_plugin_from_dir(struct deai *p, struct di_string path) {
	if (!path.data) {
		return -1;
	}

	char path_str[PATH_MAX];
	if (!di_string_to_chars(path, path_str, sizeof(path_str))) {
		return -1;
	}
	return load_plugin_from_dir_impl(p, path_str);
}

/// Change working directory
///
/// EXPORT: chdir(dir: :string): :integer
int di_chdir(struct di_object *p, struct di_string dir) {
	if (!dir.data) {
		return -EINVAL;
	}
	with_cleanup_t(char) dir_str = di_string_to_chars_alloc(dir);

	int ret = chdir(dir_str);
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
	// Recover the original proctitle memory
	ptrdiff_t proctitle_offset = di->proctitle - di->orig_proctitle;
	size_t proctitle_size = di->proctitle_end - di->proctitle;
	memcpy(di->proctitle, di->orig_proctitle, di->proctitle_end - di->proctitle);

	// Revert the changes we did to argv and environ
	for (int i = 0; environ[i]; i++) {
		if (environ[i] >= di->orig_proctitle &&
		    environ[i] < di->orig_proctitle + proctitle_size) {
			environ[i] += proctitle_offset;
		}
	}
	for (int i = 0; i < di->argc; i++) {
		di->argv[i] += proctitle_offset;
	}
	di->argv = NULL;
	di->argc = 0;
	free(di->orig_proctitle);
#endif

	// fprintf(stderr, "%d\n", p->ref_count);
	kill_all_descendants();
	ev_break(di->loop, EVBREAK_ALL);
}

/// Exit deai
///
/// EXPORT: exit(exit_code: :integer): :void
///
/// Instruct deai to exit. deai won't exit immediately when the function is called, it
/// will exit next time the control returns to the mainloop. (e.g. after your script
/// finished running).
void di_prepare_exit(struct deai *di, int ec) {
	*di->exit_code = ec;
	// Drop all the roots, this should stop the program
	di_finalize_object((struct di_object *)roots);
}

/// Exit deai
///
/// EXPORT: quit(): :void
///
/// Equivalent to :code:`exit(0)`
void di_prepare_quit(struct deai *di) {
	di_prepare_exit(di, 0);
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
	p->proctitle = argv[0];

	// Find the last argument
	uintptr_t end = (uintptr_t)p->proctitle;
	for (int i = 0; argv[i]; i++) {
		end = (uintptr_t)argv[i] + strlen(argv[i]);
	}
	for (int i = 0; environ[i]; i++) {
		end = (uintptr_t)environ[i] + strlen(environ[i]);
	}

	// Available space extends until the end of the page
	auto pgsz = getpagesize();
	end = (end / pgsz + 1) * pgsz;
	p->proctitle_end = (void *)end;

	p->orig_proctitle = malloc(p->proctitle_end - p->proctitle);
	memcpy(p->orig_proctitle, p->proctitle, p->proctitle_end - p->proctitle);

	ptrdiff_t proctitle_offset = p->orig_proctitle - p->proctitle;

	// Point argv and environ to the copy of the proctitle
	p->argc = argc;
	p->argv = argv;
	for (int i = 0; i < argc; i++) {
		// fprintf(stderr, "%p\n", argv[i]);
		argv[i] += proctitle_offset;
	}

	for (int i = 0; environ[i]; i++) {
		environ[i] += proctitle_offset;
	}
}
static void di_set_pr_name(struct deai *p, struct di_string name) {
	if (name.data) {
		memset((char *)p->proctitle, 0, p->proctitle_end - p->proctitle);
		auto nlen = name.length;

		if (p->proctitle + nlen + 1 >= p->proctitle_end) {
			nlen = p->proctitle_end - p->proctitle - 1;
		}
		strncpy((char *)p->proctitle, name.data, nlen);
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
	ret.elem_type = DI_TYPE_STRING_LITERAL;
	ret.arr = calloc(p->argc, sizeof(void *));

	const char **arr = ret.arr;
	for (int i = 0; i < p->argc; i++) {
		arr[i] = p->argv[i];
	}

	return ret;
}

int di_register_module(struct deai *p, struct di_string name, struct di_module **m) {
	int ret =
	    di_add_member_move((void *)p, name, (di_type_t[]){DI_TYPE_OBJECT}, (void **)m);
	return ret;
}

/// Register a module
///
/// EXPORT: register_module(name: :string, module: deai:module): :integer
static int
di_register_module_method(struct deai *p, struct di_string name, struct di_module *m) {
	// Don't consumer the ref, because it breaks the usual method call sementics
	return di_add_member_clonev((void *)p, name, DI_TYPE_OBJECT, m);
}

/// Execute another binary
///
/// EXPORT: exec(argv: [:string]): :integer
///
/// This call replaces the current process by running another binary. One use case for
/// this is to restart deai.
int di_exec(struct deai *p, struct di_array argv) {
	char **nargv = tmalloc(char *, argv.length + 1);
	struct di_string *strings = argv.arr;
	for (int i = 0; i < argv.length; i++) {
		nargv[i] = di_string_to_chars_alloc(strings[i]);
	}
	execvp(nargv[0], nargv);

	for (int i = 0; i < argv.length; i++) {
		free(nargv[i]);
	}
	free(nargv);
	return -1;
}

// Terminate self and all children.
//
// This is a best effort attempt, some of the children might have moved to different
// process group.
void di_terminate(struct deai *p) {
	kill(0, SIGTERM);
}

/// Add an named object as a root to keep it alive
static bool di_add_root(struct di_object *di, struct di_string key, struct di_object *obj) {
	char *buf;
	asprintf(&buf, "__root_%.*s", (int)key.length, key.data);
	int rc = di_add_member_clonev(di, di_string_borrow(buf), DI_TYPE_OBJECT, obj);
	free(buf);
	return rc == 0;
}

/// Remove an named root from roots
static bool di_remove_root(struct di_object *di, struct di_string key) {
	char *buf;
	asprintf(&buf, "__root_%.*s", (int)key.length, key.data);
	int rc = di_remove_member_raw(di, di_string_borrow(buf));
	free(buf);
	return rc == 0;
}

/// Remove all named roots
static void di_clear_roots(struct di_object *di_) {
	static const char *const root_prefix = "__root_";
	const size_t root_prefix_len = strlen(root_prefix);
	auto di = (struct di_object_internal *)di_;
	struct di_member *i, *tmp;
	HASH_ITER (hh, di->members, i, tmp) {
		if (i->name.length < root_prefix_len) {
			continue;
		}
		if (strncmp(i->name.data, root_prefix, root_prefix_len) == 0) {
			di_remove_member_raw(di_, i->name);
		}
	}
}

/// Add an unnamed root. Unlike the named roots, these roots don't need a unique name,
/// but the caller instead needs to keep a handle to the root in order to remove it.
/// The returned handle is guaranteed to be greater than 0
static uint64_t di_add_anonymous_root(struct di_object *obj, struct di_object *root) {
	auto roots = (struct di_roots *)obj;
	DI_CHECK(roots->next_anonymous_root_id != 0, "anonymous root id overflown");

	auto aroot = tmalloc(struct di_anonymous_root, 1);
	aroot->obj = di_ref_object(root);
	aroot->id = roots->next_anonymous_root_id;
	roots->next_anonymous_root_id++;
	HASH_ADD(hh, roots->anonymous_roots, id, sizeof(uint64_t), aroot);
	return aroot->id;
}

/// Remove an unnamed root.
static void di_remove_anonymous_root(struct di_object *obj, uint64_t root_handle) {
	auto roots = (struct di_roots *)obj;
	struct di_anonymous_root *aroot = NULL;
	HASH_FIND(hh, roots->anonymous_roots, &root_handle, sizeof(root_handle), aroot);
	if (aroot) {
		HASH_DEL(roots->anonymous_roots, aroot);
		di_unref_object(aroot->obj);
		free(aroot);
	}
}

static void di_roots_dtor(struct di_object *obj) {
	// Drop all the anonymous roots
	auto roots = (struct di_roots *)obj;
	struct di_anonymous_root *i, *tmp;

	// Grab a list of objects we need to free first. Because destroying an object can
	// cause another anonymous root to be removed anywhere on the list, HASH_ITER is
	// not enough to handle this scenario.
	auto total_roots = HASH_COUNT(roots->anonymous_roots);
	auto objects_to_free = tmalloc(struct di_object *, total_roots);
	size_t index = 0;
	HASH_ITER (hh, roots->anonymous_roots, i, tmp) {
		HASH_DEL(roots->anonymous_roots, i);
		objects_to_free[index++] = i->obj;
		free(i);
	}
	DI_CHECK(index == total_roots);

	for (size_t i = 0; i < total_roots; i++) {
		di_unref_object(objects_to_free[i]);
	}
	free(objects_to_free);
}

static struct di_object *di_roots_getter(struct di_object *unused di) {
	// If roots is gotten via a deai getter, we need to respect the getter semantics
	// and increment the refcount, even though normally this is not needed.
	di_ref_object((struct di_object *)roots);
	return (struct di_object *)roots;
}

int main(int argc, char *argv[]) {
#ifdef TRACK_OBJECTS
	INIT_LIST_HEAD(&all_objects);
#endif
	auto p = di_new_object_with_type(struct deai);
	di_set_type((struct di_object *)p, "deai:Core");

	roots = di_new_object_with_type(struct di_roots);
	di_set_type((struct di_object *)roots, "deai:Roots");
	DI_CHECK_OK(di_method(roots, "add", di_add_root, struct di_string, struct di_object *));
	DI_CHECK_OK(di_method(roots, "remove", di_remove_root, struct di_string));
	DI_CHECK_OK(di_method(roots, "clear", di_clear_roots));
	DI_CHECK_OK(di_method(roots, "__add_anonymous", di_add_anonymous_root,
	                      struct di_object *));
	DI_CHECK_OK(di_method(roots, "__remove_anonymous", di_remove_anonymous_root, uint64_t));
	di_set_object_dtor((struct di_object *)roots, di_roots_dtor);
	roots->next_anonymous_root_id = 1;

	// exit_code and quit cannot be owned by struct deai, because they are read
	// after struct deai is freed.
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

	DI_CHECK_OK(di_method(p, "load_plugin_from_dir", load_plugin_from_dir, struct di_string));
	DI_CHECK_OK(di_method(p, "load_plugin", load_plugin, struct di_string));
	DI_CHECK_OK(di_method(p, "register_module", di_register_module_method,
	                      struct di_string, struct di_object *));
	DI_CHECK_OK(di_method(p, "chdir", di_chdir, struct di_string));
	DI_CHECK_OK(di_method(p, "exec", di_exec, struct di_array));

	DI_CHECK_OK(di_method(p, "quit", di_prepare_quit));
	DI_CHECK_OK(di_method(p, "exit", di_prepare_exit, int));
	DI_CHECK_OK(di_method(p, "terminate", di_terminate));
#ifdef HAVE_SETPROCTITLE
	DI_CHECK_OK(di_method(p, "__set_proctitle", di_set_pr_name, struct di_string));
#endif
	auto closure = (struct di_object *)di_closure(di_dump_objects, ());
	di_member(p, "dump_objects", closure);

	DI_CHECK_OK(di_method(p, "track_object_ref", di_track_object_ref, struct di_object *));

	DI_CHECK_OK(di_method(p, "__get_roots", di_roots_getter));
	DI_CHECK_OK(di_method(p, "__get_argv", di_get_argv));

	// proctitle is a string literal, as its memory is not deai managed
	auto proctitle_getter =
	    di_new_field_getter(DI_TYPE_STRING_LITERAL, offsetof(struct deai, proctitle));
	di_member(p, "__get_proctitle", proctitle_getter);

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
	int ret = load_plugin_from_dir_impl(p, DI_PLUGIN_INSTALL_DIR);
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
	ret = di_rawcallxn(mod, di_string_borrow(method), &rt, &retd,
	                   (struct di_tuple){nargs, di_args}, &called);
	if (ret != 0) {
		fprintf(stderr, "Failed to call \"%s.%s\"\n", modname ? modname : "", method);
		exit_code = EXIT_FAILURE;
		quit = true;
	}

	if (rt == DI_TYPE_OBJECT) {
		struct di_string errmsg;
		if (di_get(retd.object, "errmsg", errmsg) == 0) {
			fprintf(stderr, "The function you called returned an error message:\n%.*s\n",
			        (int)errmsg.length, errmsg.data);
			di_free_string(errmsg);
			exit_code = EXIT_FAILURE;
			quit = true;
		}
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

	di_collect_garbage();
	bool has_cycle;
	if (di_mark_and_sweep(&has_cycle)) {
		di_dump_objects();
#ifdef UNITTESTS
		abort();
#endif
	}
	if (!quit) {
		ev_run(p->loop, 0);
	}

	di_unref_object((struct di_object *)roots);
	// Set to NULL so the leak checker can catch leaks
	roots = NULL;

	di_dump_objects();
	return exit_code;
}
