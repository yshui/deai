/* This Source Code Form is subject to the terms of the Mozilla Public
 *  * License, v. 2.0. If a copy of the MPL was not distributed with this
 *   * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <string.h>
#include <sys/utsname.h>

#include <deai/deai.h>
#include <deai/helper.h>

#include "os.h"

static char *di_env_get(struct di_module *m, const char *name) {
	const char *str = getenv(name);
	return str ? strdup(str) : NULL;
}

static void di_env_set(struct di_module *m, const char *key, const char *val) {
	if (val)
		setenv(key, val, 1);
	else
		unsetenv(key);
}

static char *di_get_hostname(struct deai *p) {
	struct utsname buf;
	if (uname(&buf) != 0)
		return NULL;
	return strdup(buf.nodename);
}

void di_init_os(struct deai *p) {
	struct di_module *m = di_new_module_with_type(struct di_module);

	struct di_object *o = di_new_object_with_type(struct di_object);

	di_method(o, "__get", di_env_get, char *);
	di_method(o, "__set", di_env_set, char *, char *);

	di_member(m, "env", o);

	di_getter(m, hostname, di_get_hostname);
	di_register_module(p, "os", &m);
}
