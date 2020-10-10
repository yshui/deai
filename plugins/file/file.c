/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <limits.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <deai/builtin/event.h>
#include <deai/deai.h>
#include <deai/helper.h>

#include "uthash.h"
#include "utils.h"

struct di_file_watch_entry {
	char *fname;
	int wd;
	UT_hash_handle hh, hh2;
};

struct di_file_watch {
	struct di_object;
	int fd;

	struct di_object *fdev;
	struct di_listener *fdev_listener;

	struct di_file_watch_entry *byname, *bywd;
};

static int di_file_ioev(struct di_weak_object *weak) {
	auto fw = (struct di_file_watch *)di_upgrade_weak_ref(weak);
	DI_CHECK(fw != NULL, "got ioev events but the listener has died");

	char evbuf[sizeof(struct inotify_event) + NAME_MAX + 1];
	struct inotify_event *ev = (void *)evbuf;
	int ret = read(fw->fd, evbuf, sizeof(evbuf));
	ptrdiff_t off = 0;
	while (off < ret) {
		char *path = "";
		if (ev->len > 0) {
			path = ev->name;
		}

		struct di_file_watch_entry *we = NULL;
		HASH_FIND_INT(fw->bywd, &ev->wd, we);
		if (!we) {
			// ???
			continue;
		}
#define emit(m, name)                                                                    \
	do {                                                                             \
		if (ev->mask & (m)) {                                                      \
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
		off += sizeof(struct inotify_event) + ev->len;
		ev = (void *)(evbuf + off);
	}
	return 0;
}

static int di_file_add_watch(struct di_file_watch *fw, const char *path) {
	if (!path) {
		return -EINVAL;
	}

	int ret = inotify_add_watch(fw->fd, path, IN_ALL_EVENTS);
	if (ret >= 0) {
		auto we = tmalloc(struct di_file_watch_entry, 1);
		we->wd = ret;
		we->fname = strdup(path);

		HASH_ADD_INT(fw->bywd, wd, we);
		HASH_ADD_KEYPTR(hh2, fw->byname, we->fname, strlen(we->fname), we);
		ret = 0;
	}
	return ret;
}

static void di_file_add_many_watch(struct di_file_watch *fw, struct di_array paths) {
	if (paths.length > 0 && paths.elem_type != DI_TYPE_STRING) {
		return;
	}
	const char **arr = paths.arr;
	for (int i = 0; i < paths.length; i++) {
		di_file_add_watch(fw, arr[i]);
	}
}

static int di_file_rm_watch(struct di_file_watch *fw, const char *path) {
	if (!path) {
		return -EINVAL;
	}

	struct di_file_watch_entry *we = NULL;
	HASH_FIND(hh2, fw->byname, path, strlen(path), we);
	if (!we) {
		return -ENOENT;
	}

	inotify_rm_watch(fw->fd, we->wd);
	HASH_DEL(fw->bywd, we);
	HASH_DELETE(hh2, fw->byname, we);
	free(we->fname);
	free(we);
	return 0;
}

static void stop_file_watcher(struct di_file_watch *fw) {
	if (!fw->fdev) {
		return;
	}

	close(fw->fd);

	struct di_file_watch_entry *we, *twe;
	HASH_ITER (hh, fw->bywd, we, twe) {
		HASH_DEL(fw->bywd, we);
		HASH_DELETE(hh2, fw->byname, we);
		free(we->fname);
		free(we);
	}
	di_unref_object(fw->fdev);
	fw->fdev = NULL;

	// listener needs to be the last thing to remove
	// because unref listeners might cause the object
	// itself to be unref'd
	di_stop_listener(fw->fdev_listener);
}

static struct di_object *di_file_new_watch(struct di_module *f, struct di_array paths) {
	if (paths.length > 0 && paths.elem_type != DI_TYPE_STRING) {
		return di_new_error("Argument needs to be an array of strings");
	}

	auto ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (ifd < 0) {
		return di_new_error("Failed to create new inotify file descriptor");
	}

	auto fw = di_new_object_with_type(struct di_file_watch);
	di_set_type((void *)fw, "deai.plugin.file:watch");
	fw->fd = ifd;
	di_set_object_dtor((void *)fw, (void *)stop_file_watcher);

	di_method(fw, "add", di_file_add_many_watch, struct di_array);
	di_method(fw, "add_one", di_file_add_watch, char *);
	di_method(fw, "remove", di_file_rm_watch, char *);
	di_method(fw, "stop", di_destroy_object);
	di_mgetm(f, event, di_new_error("Can't find event module"));
	di_callr(eventm, "fdevent", fw->fdev, fw->fd, IOEV_READ);

	auto tmpo = di_weakly_ref_object((struct di_object *)fw);
	auto cl = di_closure(di_file_ioev, (tmpo));
	fw->fdev_listener = di_listen_to(fw->fdev, "read", (void *)cl);
	di_unref_object((void *)cl);

	const char **arr = paths.arr;
	for (int i = 0; i < paths.length; i++) {
		di_file_add_watch(fw, arr[i]);
	}

	return (void *)fw;
}
PUBLIC_DEAI_API int di_plugin_init(struct deai *di) {
	auto fm = di_new_module(di);
	di_method(fm, "watch", di_file_new_watch, struct di_array);
	di_register_module(di, "file", &fm);
	return 0;
}
