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

define_trivial_cleanup(struct di_signal, free_sig);
define_trivial_cleanup(struct di_member, free_mem);

PUBLIC int di_register_signal(struct di_object *r, const char *name, int nargs,
                              di_type_t *types) {
	struct di_signal *sig = di_new_signal(nargs, types);

	int ret = di_add_value_member(r, name, false, DI_TYPE_OBJECT, sig);
	if (ret != 0)
		di_unref_object((void *)sig);

	return ret;
}

struct di_error {
	struct di_object;
	char *msg;
};

static void di_free_error(struct di_object *o) {
	struct di_error *e = (void *)o;
	free(e->msg);
}

PUBLIC struct di_object *di_new_error(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);

	char *errmsg;
	int ret = asprintf(&errmsg, fmt, ap);
	if (ret < 0)
		errmsg = strdup(fmt);

	struct di_error *err = di_new_object_with_type(struct di_error);
	err->msg = errmsg;
	err->dtor = di_free_error;

	di_add_address_member((void *)err, "errmsg", false, DI_TYPE_STRING, &err->msg);
	return (void *)err;
}

PUBLIC int di_emit_from_object(struct di_object *o, const char *name, ...) {
	struct di_object *sig;

	int rc = di_get(o, name, sig);
	if (rc != 0)
		return rc;

	if (!di_check_type(sig, "signal"))
		return -EINVAL;

	va_list ap;
	va_start(ap, name);
	rc = di_emitv((void *)sig, o, ap);
	va_end(ap);
	return rc;
}

PUBLIC struct di_listener *
di_add_listener(struct di_object *o, const char *name, struct di_object *l) {
	struct di_object *sig;
	int rc = di_get(o, name, sig);
	if (rc != 0)
		return ERR_PTR(rc);

	if (!di_check_type(sig, "signal"))
		return ERR_PTR(-EINVAL);

	auto li = di_add_listener_to_signal((void *)sig, l);
	if (IS_ERR(li))
		return li;

	di_bind_listener(li, o);
	return li;
}
