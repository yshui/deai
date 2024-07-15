/* This Source Code Form is subject to the terms of the Mozilla Public
 *  * License, v. 2.0. If a copy of the MPL was not distributed with this
 *   * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <dirent.h>
#include <string.h>
#include <sys/utsname.h>

#include <deai/deai.h>
#include <deai/helper.h>
#include <deai/object.h>

#include "common.h"        // IWYU pragma: keep

static struct di_variant di_env_get(struct di_module *m, di_string name_) {
	struct di_variant ret = {
	    .type = DI_LAST_TYPE,
	    .value = NULL,
	};
	if (!name_.data) {
		return ret;
	}

	scopedp(char) *name = di_string_to_chars_alloc(name_);
	const char *str = getenv(name);
	if (str) {
		ret.type = DI_TYPE_STRING_LITERAL;
		ret.value = malloc(sizeof(void *));
		di_copy_value(ret.type, ret.value, &str);
	}
	return ret;
}

static void di_env_set(struct di_module *m, di_string key_, di_string val_) {
	if (!key_.data || !val_.data) {
		return;
	}
	scopedp(char) *key = di_string_to_chars_alloc(key_);
	scopedp(char) *val = di_string_to_chars_alloc(val_);
	setenv(key, val, 1);
}

static void di_env_unset(struct di_module *m, di_string key_) {
	if (!key_.data) {
		return;
	}
	scopedp(char) *key = di_string_to_chars_alloc(key_);
	unsetenv(key);
}

static const char *di_get_hostname(struct deai *p) {
	struct utsname buf;
	if (uname(&buf) != 0) {
		return NULL;
	}
	return strdup(buf.nodename);
}

static di_array di_listdir(di_object *o unused, di_string path) {
	int capacity = 0;
	di_array ret = {.arr = NULL, .length = 0, .elem_type = DI_TYPE_STRING};
	scopedp(char) *c_path = di_string_to_chars_alloc(path);
	DIR *dir = opendir(c_path);
	if (!dir) {
		return ret;
	}
	struct dirent *ent = NULL;
	while ((ent = readdir(dir))) {
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
			continue;
		}
		if (capacity == ret.length) {
			capacity = capacity * 2 + 1;
			ret.arr = realloc(ret.arr, capacity * sizeof(di_string));
		}
		((di_string *)ret.arr)[ret.length] = di_string_dup(ent->d_name);
		ret.length++;
	}
	closedir(dir);
	return ret;
}

/// EXPORT: os: deai:module
///
/// OS environment
///
/// EXPORT: os.env: deai.builtin.os:Env
///
/// Environment variables
///
/// This object exposes environment variables as its members. You can get/set environment
/// variables by reading/changing its member. This will affect subsequently spawned
/// processes.
void di_init_os(di_object *di) {
	struct di_module *m = di_new_module(di);

	di_object *o = di_new_object_with_type(di_object);
	di_set_type(o, "deai.builtin.os:Env");

	di_method(o, "__get", di_env_get, di_string);
	di_method(o, "__set", di_env_set, di_string, di_string);
	di_method(o, "__delete", di_env_unset, di_string);

	di_member(m, "env", o);

	di_getter(m, hostname, di_get_hostname);
	di_method(m, "listdir", di_listdir, di_string);
	di_register_module(di, di_string_borrow("os"), &m);
}
