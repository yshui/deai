/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#define _GNU_SOURCE
#include <deai.h>
#include <helper.h>

#include <assert.h>
#include <stdio.h>

#include "utils.h"

PUBLIC int di_setv(struct di_object *o, const char *prop, di_type_t type, void *val) {
	if (!prop)
		return -EINVAL;

	char *buf;
	void *ret;
	di_type_t rtype;
	asprintf(&buf, "__set_%s", prop);

	auto m = di_find_method(o, buf);
	free(buf);
	if (m) {
		const void *args[1] = {val};

		int cret = di_call_callable((void *)m, &rtype, &ret, 1, &type, args);
		free(ret);
		return cret;
	}

	m = di_find_method(o, "__set");
	if (m) {
		const void *args[2] = {&prop, val};
		di_type_t types[2] = {DI_TYPE_STRING, type};

		int cret = di_call_callable((void *)m, &rtype, &ret, 2, types, args);
		free(ret);
		return cret;
	}

	return -ENOENT;
}

PUBLIC int
di_getv(struct di_object *o, const char *prop, di_type_t *type, void **ret) {
	if (!prop)
		return -EINVAL;

	char *buf;
	asprintf(&buf, "__get_%s", prop);

	auto m = di_find_method(o, buf);
	free(buf);
	if (m) {
		int cret = di_call_callable_v((void *)m, type, ret, DI_LAST_TYPE);
		return cret;
	}

	m = di_find_method(o, "__get");
	if (m) {
		int cret = di_call_callable_v((void *)m, type, ret, DI_TYPE_STRING,
		                              prop, DI_LAST_TYPE);
		return cret;
	}

	return -ENOENT;
}

struct _prop {
	struct di_object *obj;
	off_t offset;
	di_type_t t;
};

static int
offset_property(di_type_t *rtype, void **ret, unsigned int nargs,
                const di_type_t *atypes, const void *const *args, void *ud) {
	struct _prop *p = ud;
	assert(di_sizeof_type(p->t) > 0);
	if (nargs != 0)
		return -EINVAL;
	*ret = malloc(di_sizeof_type(p->t));
	memcpy(*ret, ((char *)p->obj) + p->offset, di_sizeof_type(p->t));
	*rtype = p->t;
	return 0;
}

static void free_ud(void *ud) {
	free(ud);
}

PUBLIC int di_register_field_getter(struct di_object *o, const char *fname,
                                    off_t offset, di_type_t type) {
	auto p = tmalloc(struct _prop, 1);
	p->obj = o;
	p->offset = offset;
	p->t = type;

	return di_register_method(
	    o, (void *)di_create_untyped_method(offset_property, fname, p, free_ud));
}
