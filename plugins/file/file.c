/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <limits.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <deai/builtins/event.h>
#include <deai/deai.h>
#include <deai/helper.h>

#include "uthash.h"
#include "utils.h"

struct di_file_watch_entry {
	const char *fname;
	int wd;
	UT_hash_handle hh, hh2;
};

typedef struct di_file_watch {
	di_object;
	int fd;
	int nsignals;

	struct di_file_watch_entry *byname, *bywd;
} di_file_watch;

define_object_cleanup(di_file_watch);
/// SIGNAL: deai.plugin.file:Watch.create(path: :string, file_name: :string)
/// A file or directory is created.
///
/// SIGNAL: deai.plugin.file:Watch.access(path: :string, file_name: :string)
/// A file was accessed.
///
/// SIGNAL: deai.plugin.file:Watch.attrib(path: :string, file_name: :string)
/// A file's metadata was changed.
///
/// SIGNAL: deai.plugin.file:Watch.close-write(path: :string, file_name: :string)
/// A file opened for writing was closed.
///
/// SIGNAL: deai.plugin.file:Watch.close-nowrite(path: :string, file_name: :string)
/// A file or directory not opened for iting was closed.
///
/// SIGNAL: deai.plugin.file:Watch.delete(path: :string, file_name: :string)
/// A file or directory was deleted from watched directory.
///
/// SIGNAL: deai.plugin.file:Watch.delete-self(path: :string, file_name: :string)
/// A watched file or directory was itself deleted.
///
/// SIGNAL: deai.plugin.file:Watch.modify(path: :string, file_name: :string)
/// A file was modified.
///
/// SIGNAL: deai.plugin.file:Watch.move-self(path: :string, file_name: :string)
/// A watched file or directory was itself moved
///
/// SIGNAL: deai.plugin.file:Watch.open(path: :string, file_name: :string)
/// A file or directory was opened
///
/// SIGNAL: deai.plugin.file:Watch.move-from(path: :string, file_name: :string, cookie:
/// :integer) A file in a watched directory was renamed to a new place.
///
/// Arguments:
///
/// - cookie unique integer associated with this move, can be used to pair this event with
///          a :lua:sgnl:`move-to` event.
///
/// SIGNAL: deai.plugin.file:Watch.move-to(path: :string, file_name: :string, cookie:
/// :integer) A file was renamed into a watched directory.
///
/// Arguments:
///
/// - cookie unique integer associated with this move, can be used to pair this event with
///          a :lua:sgnl:`move-from` event.
static int di_file_ioev(struct di_weak_object *weak) {
	scopedp(di_file_watch) *fw = (void *)di_upgrade_weak_ref(weak);
	DI_CHECK(fw != NULL, "got ioev events but the listener has died");

	char evbuf[sizeof(struct inotify_event) + NAME_MAX + 1];
	struct inotify_event *ev = (void *)evbuf;
	int ret = read(fw->fd, evbuf, sizeof(evbuf));
	ptrdiff_t off = 0;
	while (off < ret) {
		const char *path = "";
		if (ev->len > 0) {
			path = ev->name;
		}

		struct di_file_watch_entry *we = NULL;
		HASH_FIND_INT(fw->bywd, &ev->wd, we);
		if (!we) {
			// This might happen if the user removed a watch with lingering
			// evetns.
			goto next;
		}
#define emit(m, name)                                                                    \
	do {                                                                             \
		if (ev->mask & (m)) {                                                    \
			di_emit(fw, name, we->fname, path);                              \
		}                                                                        \
	} while (0)
		emit(IN_CREATE, "create");
		emit(IN_ACCESS, "access");
		emit(IN_ATTRIB, "attrib");
		emit(IN_CLOSE_WRITE, "close-write");
		emit(IN_CLOSE_NOWRITE, "close-nowrite");
		emit(IN_DELETE, "delete");
		emit(IN_DELETE_SELF, "delete-self");
		emit(IN_MODIFY, "modify");
		emit(IN_MOVE_SELF, "move-self");
		emit(IN_OPEN, "open");
		if (ev->mask & IN_MOVED_FROM) {
			di_emit(fw, "moved-from", we->fname, path, ev->cookie);
		}
		if (ev->mask & IN_MOVED_TO) {
			di_emit(fw, "moved-to", we->fname, path, ev->cookie);
		}
#undef emit
	next:
		off += sizeof(struct inotify_event) + ev->len;
		ev = (void *)(evbuf + off);
	}
	return 0;
}

/// Add a file
///
/// EXPORT: deai.plugin.file:Watch.add_one(path: :string): :integer
///
/// Add a single new file to a watch, returns 0 if successful.
static int di_file_add_watch(struct di_file_watch *fw, di_string path) {
	if (!path.data) {
		return -EINVAL;
	}

	char *path_str = di_string_to_chars_alloc(path);

	int ret = inotify_add_watch(fw->fd, path_str, IN_ALL_EVENTS);
	if (ret >= 0) {
		auto we = tmalloc(struct di_file_watch_entry, 1);
		we->wd = ret;
		we->fname = path_str;

		HASH_ADD_INT(fw->bywd, wd, we);
		HASH_ADD_KEYPTR(hh2, fw->byname, we->fname, path.length, we);
		ret = 0;
	}
	return ret;
}

/// Add files
///
/// EXPORT: deai.plugin.file:Watch.add(paths: [:string]): :integer
///
/// Add new files to a watch, returns 0 if successful.
static int di_file_add_many_watch(struct di_file_watch *fw, di_array paths) {
	if (paths.length == 0) {
		return 0;
	}
	if (paths.elem_type != DI_TYPE_STRING && paths.elem_type != DI_TYPE_STRING_LITERAL) {
		return -EINVAL;
	}
	int ret = 0;
	if (paths.elem_type == DI_TYPE_STRING) {
		di_string *arr = paths.arr;
		for (int i = 0; i < paths.length; i++) {
			ret = di_file_add_watch(fw, arr[i]);
			if (ret != 0) {
				break;
			}
		}
	} else {
		const char **arr = paths.arr;
		for (int i = 0; i < paths.length; i++) {
			ret = di_file_add_watch(fw, di_string_borrow(arr[i]));
			if (ret != 0) {
				break;
			}
		}
	}
	return ret;
}

/// Remove a file
///
/// EXPORT: deai.plugin.file:Watch.remove(path: :string): :integer
///
/// Returns 0 if successful. If the file is not in the watch, return :code:`-ENOENT`.
static int di_file_rm_watch(struct di_file_watch *fw, di_string path) {
	if (!path.data) {
		return -EINVAL;
	}

	struct di_file_watch_entry *we = NULL;
	HASH_FIND(hh2, fw->byname, path.data, path.length, we);
	if (!we) {
		return -ENOENT;
	}

	inotify_rm_watch(fw->fd, we->wd);
	HASH_DEL(fw->bywd, we);
	HASH_DELETE(hh2, fw->byname, we);
	free((char *)we->fname);
	free(we);
	return 0;
}

static void stop_file_watcher(struct di_file_watch *fw) {
	close(fw->fd);

	struct di_file_watch_entry *we, *twe;
	HASH_ITER (hh, fw->bywd, we, twe) {
		HASH_DEL(fw->bywd, we);
		HASH_DELETE(hh2, fw->byname, we);
		free((char *)we->fname);
		free(we);
	}
}

static void di_file_new_signal(di_object *fw_, di_string member_name, di_object *sig) {
	if (!di_string_starts_with(member_name, "__signal_")) {
		return;
	}

	if (di_add_member_clone(fw_, member_name, DI_TYPE_OBJECT, &sig) != 0) {
		return;
	}

	auto fw = (struct di_file_watch *)fw_;
	fw->nsignals += 1;
	if (fw->nsignals == 1) {
		// Start fdevent
		scoped_di_weak_object *weak_eventm = NULL;
		DI_CHECK_OK(di_get(fw, "__weak_event_module", weak_eventm));
		scoped_di_object *event_module = di_upgrade_weak_ref(weak_eventm);
		if (event_module == NULL) {
			// Event module went away, deai is exiting?
			return;
		}

		scoped_di_object *fdevent = NULL;
		DI_CHECK_OK(di_callr(event_module, "fdevent", fdevent, fw->fd));

		scoped_di_closure *cl = di_make_closure(di_file_ioev, ((di_object *)fw));
		auto listen_handle =
		    di_listen_to(fdevent, di_string_borrow("read"), (void *)cl);
		DI_CHECK_OK(di_call(listen_handle, "auto_stop", true));
		di_member(fw, "__inotify_fd_event_read_listen_handle", listen_handle);
	}
}

static void di_file_delete_signal(di_object *fw_, di_string member_name) {
	if (!di_string_starts_with(member_name, "__signal_")) {
		return;
	}

	if (di_remove_member_raw(fw_, member_name) != 0) {
		return;
	}

	auto fw = (struct di_file_watch *)fw_;
	fw->nsignals -= 1;
	if (fw->nsignals == 0) {
		// Drop listen handle to stop it
		di_remove_member_raw(
		    fw_, di_string_borrow("__inotify_fd_event_read_listen_handle"));
	}
}

/// Create a new file watch
///
/// EXPORT: file.watch(paths): deai.plugin.file:Watch
///
/// The returned watch is set to monitor a given set of file from the start. But this set
/// can be changed later.
///
/// Arguments:
///
/// - paths([:string]) an array of paths to watch
static di_object *di_file_new_watch(struct di_module *f, di_array paths) {
	if (paths.length > 0 && paths.elem_type != DI_TYPE_STRING &&
	    paths.elem_type != DI_TYPE_STRING_LITERAL) {
		return di_new_error("Argument needs to be an array of strings");
	}

	auto ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (ifd < 0) {
		return di_new_error("Failed to create new inotify file descriptor");
	}

	auto fw = di_new_object_with_type(struct di_file_watch);
	di_set_type((void *)fw, "deai.plugin.file:Watch");
	fw->fd = ifd;
	di_set_object_dtor((void *)fw, (void *)stop_file_watcher);

	di_method(fw, "add", di_file_add_many_watch, di_array);
	di_method(fw, "add_one", di_file_add_watch, di_string);
	di_method(fw, "remove", di_file_rm_watch, di_string);
	di_method(fw, "__set", di_file_new_signal, di_string, di_object *);
	di_method(fw, "__delete", di_file_delete_signal, di_string);
	di_mgetm(f, event, di_new_error("Can't find event module"));

	auto weak_eventm = di_weakly_ref_object(eventm);
	di_member(fw, "__weak_event_module", weak_eventm);

	if (di_file_add_many_watch(fw, paths) != 0) {
		di_unref_object((di_object *)fw);
		return di_new_error("Failed to add watches");
	}
	return (void *)fw;
}

/// File events
///
/// EXPORT: file: deai:module
///
/// This module allows you to create event sources for monitoring file changes.
static struct di_module *di_new_file(struct deai *di) {
	auto fm = di_new_module(di);
	di_method(fm, "watch", di_file_new_watch, di_array);
	return fm;
}
DEAI_PLUGIN_ENTRY_POINT(di) {
	auto fm = di_new_file(di);
	di_register_module(di, di_string_borrow("file"), &fm);
	return 0;
}
