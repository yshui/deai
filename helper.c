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

	di_add_ref_member((void *)err, "errmsg", false, DI_TYPE_STRING, &err->msg);
	return (void *)err;
}

PUBLIC int di_gmethod(struct di_object *o, const char *name, di_fn_t fn) {
	with_object_cleanup(di_object) m = di_new_object_with_type(struct di_object);
	m->call = (void *)fn;

	return di_add_value_member(o, name, false, DI_TYPE_OBJECT, m);
}

typedef struct {
	struct di_object;
	struct di_object *proxy;
	struct di_listener *l;
	char *signal;
} _di_proxied_signal;

static int _emit_proxied_signal(struct di_object *o, di_type_t *rt, void **ret,
                                struct di_tuple t) {
	auto p = (_di_proxied_signal *)o;
	di_emitn(p->proxy, p->signal, t);
	return 0;
}

static void _free_proxied_signal(struct di_object *_sig) {
	auto sig = (_di_proxied_signal *)_sig;
	free(sig->signal);
	di_stop_listener(sig->l);
}

PUBLIC int di_proxy_signal(struct di_object *src, struct di_object *proxy,
                           const char *srcsig, const char *proxysig) {
	if (strncmp(srcsig, "__", 2) == 0)
		return -EPERM;

	auto c = di_new_module_with_type(_di_proxied_signal);
	c->dtor = _free_proxied_signal;
	c->signal = strdup(proxysig);
	c->proxy = proxy;        // proxied signal should die when proxy die
	c->call = _emit_proxied_signal;

	char *buf;
	asprintf(&buf, "__del_signal_%s", proxysig);

	auto cl = di_closure(_free_proxied_signal, true, ((struct di_object *)c));
	int ret = di_add_value_member(proxy, buf, false, DI_TYPE_OBJECT, cl);
	di_unref_object((void *)cl);
	free(buf);

	if (ret != 0) {
		di_unref_object((void *)c);
		return ret;
	}

	auto l = di_listen_to(src, srcsig, (void *)c);
	di_set_detach(l, _free_proxied_signal, (void *)c);
	return 0;
}
