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
#include <deai/error.h>
#include <deai/helper.h>
#include <deai/type.h>

#include <config.h>

#include "di_internal.h"
#include "event.h"
#include "log.h"
#include "os.h"
#include "spawn.h"
#include "uthash.h"

static bool load_plugin_impl(struct deai *p, char *sopath) {
	if (*sopath != '/') {
		fprintf(stderr, "Plugin path must be absolute: %s\n", sopath);
		return false;
	}

	void *handle = dlopen(sopath, RTLD_NOW | RTLD_LOCAL);

	if (!handle) {
		fprintf(stderr, "Failed to load %s: %s\n", sopath, dlerror());
		return false;
	}

	init_fn_t init_fn = dlsym(handle, "di_plugin_init");
	if (!init_fn) {
		fprintf(stderr, "%s doesn't have a di_plugin_init function\n", sopath);
		return false;
	}

	init_fn((di_object *)p);
	return true;
}

/// Load a single plugin
///
/// EXPORT: load_plugin(file: :string): :void
static void load_plugin(struct deai *p, di_string sopath) {
	if (!sopath.data) {
		return;
	}

	scopedp(char) *sopath_str = di_string_to_chars_alloc(sopath);
	bool success = load_plugin_impl(p, sopath_str);
	if (!success) {
		di_throw(di_new_error("Failed to load plugin"));
	}
}

static int load_plugin_from_dir_impl(struct deai *di, const char *path) {

	char rpath[PATH_MAX];
	if (realpath(path, rpath) == NULL) {
		return -1;
	}

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
			if (asprintf(&sopath, "%s/%s", rpath, dent->d_name) >= 0) {
				load_plugin_impl(di, sopath);
				free(sopath);
			}
		}
	}
	closedir(dir);
	return 0;
}

/// Load plugins from a directory
///
/// EXPORT: load_plugin_from_dir(path: :string): :integer
static int load_plugin_from_dir(struct deai *p, di_string path) {
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
int di_chdir(di_object *p, di_string dir) {
	if (!dir.data) {
		return -EINVAL;
	}
	scopedp(char) *dir_str = di_string_to_chars_alloc(dir);

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

void di_dtor(di_object *obj) {
	auto di = (struct deai *)obj;
	*di->quit = true;

#ifdef HAVE_SETPROCTITLE
	// Recover the original proctitle memory
	ptrdiff_t proctitle_offset = di->proctitle - di->orig_proctitle;
	size_t proctitle_size = di->proctitle_end - di->proctitle;
	memcpy(di->proctitle, di->orig_proctitle, di->proctitle_end - di->proctitle);

	// Revert the changes we did to argv and environ
	for (int i = 0; environ[i]; i++) {
		if (environ[i] >= di->orig_proctitle && environ[i] < di->orig_proctitle + proctitle_size) {
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
	di_finalize_object((di_object *)roots);
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
static void di_set_pr_name(struct deai *p, di_string name) {
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

static di_array di_get_argv(struct deai *p) {
	di_array ret;
	ret.length = p->argc;
	ret.elem_type = DI_TYPE_STRING_LITERAL;
	ret.arr = calloc(p->argc, sizeof(void *));

	const char **arr = ret.arr;
	for (int i = 0; i < p->argc; i++) {
		arr[i] = p->argv[i];
	}

	return ret;
}

int di_register_module(di_object *p, di_string name, struct di_module **m) {
	int ret = di_add_member_move(p, name, (di_type[]){DI_TYPE_OBJECT}, (void **)m);
	return ret;
}

/// Register a module
///
/// EXPORT: register_module(name: :string, module: deai:module): :integer
static int di_register_module_method(struct deai *p, di_string name, struct di_module *m) {
	// Don't consumer the ref, because it breaks the usual method call sementics
	return di_add_member_clonev((void *)p, name, DI_TYPE_OBJECT, m);
}

/// Execute another binary
///
/// EXPORT: exec(argv: [:string]): :integer
///
/// This call replaces the current process by running another binary. One use case for
/// this is to restart deai.
int di_exec(struct deai *p, di_array argv) {
	char **nargv = tmalloc(char *, argv.length + 1);
	di_string *strings = argv.arr;
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
static bool di_add_root(di_object *di, di_string key, di_object *obj) {
	scoped_di_string root_key = di_string_printf("___root_%.*s", (int)key.length, key.data);
	return di_add_member_clonev(di, root_key, DI_TYPE_OBJECT, obj) == 0;
}

/// Remove an named root from roots
static bool di_remove_root(di_object *di, di_string key) {
	scoped_di_string root_key = di_string_printf("___root_%.*s", (int)key.length, key.data);
	return di_delete_member_raw(di, root_key) == 0;
}

/// Remove all named roots
static void di_clear_roots(di_object *di_) {
	static const char *const root_prefix = "___root_";
	const size_t root_prefix_len = strlen(root_prefix);
	auto di = (di_object_internal *)di_;
	struct di_member *i, *tmp;
	HASH_ITER (hh, di->members, i, tmp) {
		if (i->name.length < root_prefix_len) {
			continue;
		}
		if (strncmp(i->name.data, root_prefix, root_prefix_len) == 0) {
			di_delete_member_raw(di_, i->name);
		}
	}
}

/// Add an unnamed root.
///
/// EXPORT: deai:Roots.add_anonymous(root: :object): :boolean
///
/// Unlike the named roots, these roots don't need a unique name, but the same object
/// cannot be added twice. Returns true if the root was added, false if it was already
/// there.
static bool di_add_anonymous_root(di_object *obj, di_object *root) {
	auto roots = (struct di_roots *)obj;

	struct di_anonymous_root *aroot = NULL;
	HASH_FIND_PTR(roots->anonymous_roots, &root, aroot);
	if (aroot != NULL) {
		return false;
	}
	aroot = tmalloc(struct di_anonymous_root, 1);
	aroot->obj = di_ref_object(root);
	HASH_ADD_PTR(roots->anonymous_roots, obj, aroot);
	return true;
}

/// Remove an unnamed root.
///
/// EXPORT: deai:Roots.remove_anonymous(root: :object): :boolean
///
/// Returns true if the root was removed, false if it does not exist.
static bool di_remove_anonymous_root(di_object *obj, di_object *root) {
	auto roots = (struct di_roots *)obj;
	struct di_anonymous_root *aroot = NULL;
	HASH_FIND_PTR(roots->anonymous_roots, &root, aroot);
	if (aroot) {
		HASH_DEL(roots->anonymous_roots, aroot);
		di_unref_object(aroot->obj);
		free(aroot);
		return true;
	}
	return false;
}

static void di_roots_dtor(di_object *obj) {
	// Drop all the anonymous roots
	auto roots = (struct di_roots *)obj;
	struct di_anonymous_root *i, *tmp;

	// Grab a list of objects we need to free first. Because destroying an object can
	// cause another anonymous root to be removed anywhere on the list, HASH_ITER is
	// not enough to handle this scenario.
	auto total_roots = HASH_COUNT(roots->anonymous_roots);
	auto objects_to_free = tmalloc(di_object *, total_roots);
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

/// Get roots
///
/// EXPORT: roots: deai:Roots
static di_object *di_roots_getter(di_object *unused di) {
	// If roots is gotten via a deai getter, we need to respect the getter semantics
	// and increment the refcount, even though normally this is not needed.
	di_ref_object((di_object *)roots);
	return (di_object *)roots;
}

static const char *di_get_plugin_install_dir(di_object *p unused) {
	return DI_PLUGIN_INSTALL_DIR;
}

int main(int argc, char *argv[]) {
#ifdef TRACK_OBJECTS
	INIT_LIST_HEAD(&all_objects);
#endif
	auto p = di_new_object_with_type(struct deai);
	di_set_type((di_object *)p, "deai:Core");

	roots = di_new_object_with_type(struct di_roots);
	di_set_type((di_object *)roots, "deai:Roots");
	DI_CHECK_OK(di_method(roots, "add", di_add_root, di_string, di_object *));
	DI_CHECK_OK(di_method(roots, "remove", di_remove_root, di_string));
	DI_CHECK_OK(di_method(roots, "clear", di_clear_roots));
	DI_CHECK_OK(di_method(roots, "add_anonymous", di_add_anonymous_root, di_object *));
	DI_CHECK_OK(di_method(roots, "remove_anonymous", di_remove_anonymous_root, di_object *));
	di_set_object_dtor((di_object *)roots, di_roots_dtor);

	// exit_code and quit cannot be owned by struct deai, because they are read
	// after struct deai is freed.
	int exit_code = 0;
	bool quit = false;
	p->loop = EV_DEFAULT;
	p->exit_code = &exit_code;
	p->quit = &quit;
	p->dtor = di_dtor;

	// We want to be our own process group leader
	setpgid(0, 0);

	// (1) Initialize builtin modules first
	di_init_event((di_object *)p);
	di_init_log((di_object *)p);
	di_init_os((di_object *)p);
	di_init_spawn((di_object *)p);

	if (argc < 2) {
		printf("Usage: %s <module>.<method> <arg1> <arg2> ...\n", argv[0]);
		exit(1);
	}

	char *resources_dir = getenv("DEAI_RESOURCES_DIR");
	if (resources_dir) {
		di_string resources_dir_str = di_string_dup(resources_dir);
		DI_CHECK_OK(di_member(p, "resources_dir", resources_dir_str));
	} else {
		const char *builtin_resources_dir = DI_RESOURCES_DIR;
		DI_CHECK_OK(di_member(p, "resources_dir", builtin_resources_dir));
	}

	DI_CHECK_OK(di_method(p, "load_plugin_from_dir", load_plugin_from_dir, di_string));
	DI_CHECK_OK(di_method(p, "load_plugin", load_plugin, di_string));
	DI_CHECK_OK(di_method(p, "register_module", di_register_module_method, di_string,
	                      di_object *));
	DI_CHECK_OK(di_method(p, "chdir", di_chdir, di_string));
	DI_CHECK_OK(di_method(p, "exec", di_exec, di_array));

	DI_CHECK_OK(di_method(p, "quit", di_prepare_quit));
	DI_CHECK_OK(di_method(p, "exit", di_prepare_exit, int));
	DI_CHECK_OK(di_method(p, "terminate", di_terminate));
#ifdef HAVE_SETPROCTITLE
	DI_CHECK_OK(di_method(p, "__set_proctitle", di_set_pr_name, di_string));
#endif
	DI_CHECK_OK(di_getter(p, DI_PLUGIN_INSTALL_DIR, di_get_plugin_install_dir));
	auto closure = (di_object *)di_make_closure(di_dump_objects, ());
	di_member(p, "dump_objects", closure);

	DI_CHECK_OK(di_method(p, "track_object_ref", di_track_object_ref, di_object *));

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

	// Create a scope so everything in here will be freed
	// before we start the mainloop
	{
		// (2) Parse commandline
		scopedp(char) *modname = NULL;
		scopedp(char) *method = strchr(argv[1], '.');
		if (method) {
			modname = strndup(argv[1], method - argv[1]);
			method = strdup(method + 1);
		} else {
			method = strdup(argv[1]);
		}

		scoped_di_tuple args = DI_TUPLE_INIT;
		args.elements = tmalloc(struct di_variant, argc - 1);
		args.elements[0].type = DI_TYPE_OBJECT;
		args.length = argc - 1;
		int nargs = 1;        // The first argument is the module object
		char *endptr = NULL;
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
			case 'i':;        // Integer
				int64_t val = strtoll(argv[i] + 2, &endptr, 10);
				if (*endptr != '\0') {
					fprintf(stderr, "Invalid integer: %s\n", argv[i] + 2);
					exit(EXIT_FAILURE);
				}
				args.elements[nargs] = di_alloc_variant(val);
				break;
			case 's':        // String
				args.elements[nargs] = di_alloc_variant(di_string_dup(argv[i] + 2));
				break;
			case 'f':;        // Float
				double fval = strtod(argv[i] + 2, &endptr);
				if (*endptr != '\0') {
					fprintf(stderr, "Invalid float: %s\n", argv[i] + 2);
					exit(EXIT_FAILURE);
				}
				args.elements[nargs] = di_alloc_variant(fval);
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
			fprintf(stderr, "Failed to load plugins from \"%s\".\n", DI_PLUGIN_INSTALL_DIR);
		}
		const char *additional_plugin_dirs = getenv("DEAI_EXTRA_PLUGIN_DIRS");
		while (additional_plugin_dirs) {
			const char *next = strchr(additional_plugin_dirs, ':');
			if (!next) {
				next = additional_plugin_dirs + strlen(additional_plugin_dirs);
			}
			char *dir = strndup(additional_plugin_dirs, next - additional_plugin_dirs);
			if (load_plugin_from_dir_impl(p, dir) != 0) {
				fprintf(stderr, "Failed to load plugins from \"%s\".\n", dir);
			}
			free(dir);
			additional_plugin_dirs = *next ? next + 1 : NULL;
		}
		const char *additional_plugins = getenv("DEAI_EXTRA_PLUGINS");
		while (additional_plugins) {
			const char *next = strchr(additional_plugins, ':');
			if (!next) {
				next = additional_plugins + strlen(additional_plugins);
			}
			char *plugin = strndup(additional_plugins, next - additional_plugins);
			if (!load_plugin_impl(p, plugin)) {
				fprintf(stderr, "Failed to load plugin \"%s\".\n", plugin);
			}
			free(plugin);
			additional_plugins = *next ? next + 1 : NULL;
		}

		scoped_di_object *mod = NULL;
		if (modname) {
			ret = di_get(p, modname, mod);
			if (ret != 0) {
				fprintf(stderr, "Module \"%s\" not found\n", modname);
				exit(EXIT_FAILURE);
			}
		} else {
			mod = di_ref_object((di_object *)p);
		}

		di_type rt;
		di_value retd;
		di_object *method_obj = NULL;
		scoped_di_object *error_obj = NULL;
		int rc;
		args.elements[0] = di_alloc_variant(di_ref_object(mod));
		if (di_rawget_borrowed(mod, method, method_obj) != 0) {
			if (modname != NULL) {
				fprintf(stderr, "Method \"%s\" not found in module \"%s\"\n", method, modname);
			} else {
				fprintf(stderr, "Method \"%s\" not found in main module\n", method);
			}
			exit_code = EXIT_FAILURE;
			quit = true;
		} else if ((rc = di_call_object_catch(method_obj, &rt, &retd, args, &error_obj)) != 0) {
			fprintf(stderr, "Failed to call \"%s.%s\": %d\n", modname ? modname : "", method, rc);
			exit_code = EXIT_FAILURE;
			quit = true;
		} else {
			di_free_value(rt, &retd);

			if (error_obj != NULL) {
				scoped_di_string error_message = di_object_to_string(error_obj, NULL);
				fprintf(stderr, "The function you called returned an error message:\n%.*s\n",
				        (int)error_message.length, error_message.data);
				exit_code = EXIT_FAILURE;
				quit = true;
			}
		}
		di_unref_object((void *)p);
	}
	// (4) Start mainloop

	di_collect_garbage();
	di_dump_objects();
	if (!quit) {
		ev_run(p->loop, 0);
	}

	di_unref_object((di_object *)roots);
	// Set to NULL so the leak checker can catch leaks
	roots = NULL;

	di_dump_objects();
	return exit_code;
}
