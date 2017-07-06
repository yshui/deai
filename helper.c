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
define_object_cleanup(di_signal);

PUBLIC int di_register_signal(struct di_object *r, const char *name, int nargs,
                              di_type_t *types) {
	with_object_cleanup(di_signal) sig = di_new_signal(nargs, types);
	return di_add_value_member(r, name, false, DI_TYPE_OBJECT, sig);
}

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

static int get_signal(struct di_object *o, const char *name, struct di_signal **ret) {
	struct di_object *sig;
	int rc = di_get(o, name, sig);
	if (rc != 0)
		return rc;

	if (!di_check_type(sig, "signal"))
		return -EINVAL;

	*ret = (void *)sig;
	return 0;
}

PUBLIC int
di_emitn_from_object(struct di_object *o, const char *name, const void *const *args) {
	assert(o == *(struct di_object **)args[0]);
	with_object_cleanup(di_signal) sig = NULL;
	int rc = get_signal(o, name, &sig);
	if (rc != 0)
		return rc;

	return di_emitn(sig, o, args);
}

PUBLIC int di_emit_from_object(struct di_object *o, const char *name, ...) {
	with_object_cleanup(di_signal) sig = NULL;
	int rc = get_signal(o, name, &sig);
	if (rc != 0)
		return rc;

	va_list ap;
	va_start(ap, name);
	rc = di_emitv(sig, o, ap);
	va_end(ap);
	return rc;
}

PUBLIC struct di_listener *
di_add_listener(struct di_object *o, const char *name, struct di_object *l) {
	with_object_cleanup(di_signal) sig = NULL;
	int rc = get_signal(o, name, &sig);
	if (rc != 0)
		return ERR_PTR(rc);

	auto li = di_add_listener_to_signal((void *)sig, l);
	if (IS_ERR(li))
		return li;

	return li;
}

PUBLIC int di_gmethod(struct di_object *o, const char *name, di_fn_t fn) {
	with_object_cleanup(di_object) m = di_new_object_with_type(struct di_object);
	m->call = (void *)fn;

	return di_add_value_member(o, name, false, DI_TYPE_OBJECT, m);
}
