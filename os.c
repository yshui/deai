/* This Source Code Form is subject to the terms of the Mozilla Public
 *  * License, v. 2.0. If a copy of the MPL was not distributed with this
 *   * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <string.h>
#include <sys/utsname.h>

#include <deai/deai.h>
#include <deai/helper.h>

#include "os.h"

static struct di_variant di_env_get(struct di_module *m, const char *nonnull name) {
	const char *str = getenv(name);
	struct di_variant ret;
	if (str) {
		ret.type = DI_TYPE_STRING_LITERAL;
		ret.value = malloc(sizeof(void *));
		ret.value->string_literal = str;
	} else {
		ret.type = DI_LAST_TYPE;
		ret.value = NULL;
	}
	return ret;
}

static void di_env_set(struct di_module *m, const char *nonnull key, const char *nonnull val) {
	setenv(key, val, 1);
}

static void di_env_unset(struct di_module *m, const char *nonnull key) {
	unsetenv(key);
}

static char *di_get_hostname(struct deai *p) {
	struct utsname buf;
	if (uname(&buf) != 0) {
		return NULL;
	}
	return strdup(buf.nodename);
}

void di_init_os(struct deai *di) {
	struct di_module *m = di_new_module(di);

	struct di_object *o = di_new_object_with_type(struct di_object);
	di_set_type(o, "deai.builtin.os:Env");

	di_method(o, "__get", di_env_get, const char *);
	di_method(o, "__set", di_env_set, const char *, const char *);
	di_method(o, "__delete", di_env_unset, const char *);

	di_member(m, "env", o);

	di_getter(m, hostname, di_get_hostname);
	di_register_module(di, "os", &m);
}
