/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#define _GNU_SOURCE
#include <deai/deai.h>
#include <deai/helper.h>

#include <assert.h>
#include <stdio.h>

#include "utils.h"

define_object_cleanup(di_object);

struct di_error {
	struct di_object;
	char *msg;
};

PUBLIC struct di_object *di_new_error(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);

	char *errmsg;
	int ret = asprintf(&errmsg, fmt, ap);
	if (ret < 0)
		errmsg = strdup(fmt);

	struct di_error *err = di_new_object_with_type(struct di_error);
	err->msg = errmsg;

	di_add_address_member((void *)err, "errmsg", false, DI_TYPE_STRING, &err->msg);
	return (void *)err;
}

PUBLIC int di_gmethod(struct di_object *o, const char *name, di_fn_t fn) {
	with_object_cleanup(di_object) m = di_new_object_with_type(struct di_object);
	m->call = (void *)fn;

	return di_add_value_member(o, name, false, DI_TYPE_OBJECT, m);
}
