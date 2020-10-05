/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai/deai.h>
#include <deai/helper.h>

#include <assert.h>
#include <stdio.h>

#include "utils.h"

struct di_error {
	struct di_object;
	char *msg;
};

PUBLIC void di_free_error(struct di_object *o) {
	auto err = (struct di_error *)o;
	free(err->msg);
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

	di_add_member_ref((void *)err, "errmsg", DI_TYPE_STRING, &err->msg);
	err->dtor = di_free_error;
	return (void *)err;
}

PUBLIC int di_gmethod(struct di_object *o, const char *name, void (*fn)(void)) {
	with_object_cleanup(di_object) m = di_new_object_with_type(struct di_object);
	m->call = (void *)fn;

	return di_add_member_clone(o, name, DI_TYPE_OBJECT, m);
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

static void _del_proxied_signal(struct di_object *_sig) {
	auto sig = (_di_proxied_signal *)_sig;
	di_stop_listener(sig->l);
	free(sig->signal);

	// Remove the __del_signal, this should free the proxied_signal object
	char *buf;
	asprintf(&buf, "__del_signal_%s", sig->signal);
	di_remove_member(sig->proxy, buf);
	free(buf);
}

// Add a listener to src for "srcsig". When "srcsig" is emitted, the proxy will emit
// "proxysig"
// This is intended to be called in proxy's "__new_signal" method
PUBLIC int di_proxy_signal(struct di_object *src, const char *srcsig,
                           struct di_object *proxy, const char *proxysig) {
	if (strncmp(srcsig, "__", 2) == 0)
		return -EPERM;

	auto c = di_new_object_with_type(_di_proxied_signal);
	c->signal = strdup(proxysig);
	c->proxy = proxy;        // proxied signal should die when proxy die
	c->call = _emit_proxied_signal;

	char *buf;
	asprintf(&buf, "__del_signal_%s", proxysig);

	// Let the __del_signal method hold the only ref to proxied_signal
	auto cl = di_closure(_del_proxied_signal, false, ((struct di_object *)c));
	int ret = di_add_member_move(proxy, buf, (di_type_t[]){DI_TYPE_OBJECT}, (void **)&cl);
	di_unref_object((void *)c);
	free(buf);

	if (ret != 0)
		return ret;

	auto l = di_listen_to(src, srcsig, (void *)c);
	di_set_detach(l, _del_proxied_signal, (void *)c);
	c->l = l;
	return 0;
}
