/* This Source Code Form is subject to the terms of the Mozilla Public
 *  * License, v. 2.0. If a copy of the MPL was not distributed with this
 *   * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai.h>

#include <string.h>

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
	struct di_module *m = di_new_module_with_type("env", struct di_module);

	di_register_typed_method((void *)m, (di_fn_t)di_env_get, "__get",
	                         DI_TYPE_STRING, 1, DI_TYPE_STRING);

	di_register_typed_method((void *)m, (di_fn_t)di_env_set, "__set",
	                         DI_TYPE_VOID, 2, DI_TYPE_STRING, DI_TYPE_STRING);
	di_register_module(p, m);
}
