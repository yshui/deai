/* This Source Code Form is subject to the terms of the Mozilla Public
 *  * License, v. 2.0. If a copy of the MPL was not distributed with this
 *   * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <string.h>

#include <deai/deai.h>
#include <deai/helper.h>

#include "env.h"

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

void di_init_env(struct deai *p) {
	struct di_module *m = di_new_module_with_type(struct di_module);

	di_method(m, "__get", di_env_get, char *);
	di_method(m, "__set", di_env_set, char *, char *);

	di_register_module(p, "env", m);
	di_unref_object((void *)m);
}
