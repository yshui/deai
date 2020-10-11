/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai/deai.h>
#include <deai/helper.h>

#include <assert.h>
#include <stdio.h>

#include "di_internal.h"
#include "utils.h"

struct di_error {
	struct di_object_internal;
	char *msg;
};

void di_free_error(struct di_object *o) {
	auto err = (struct di_error *)o;
	free(err->msg);
}

struct di_object *di_new_error(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);

	char *errmsg;
	int ret = asprintf(&errmsg, fmt, ap);
	if (ret < 0) {
		errmsg = strdup(fmt);
	}

	struct di_error *err = di_new_object_with_type(struct di_error);
	err->msg = errmsg;

	auto errmsg_getter =
	    di_new_field_getter(DI_TYPE_STRING_LITERAL, offsetof(struct di_error, msg));
	di_add_member_clone((void *)err, "__get_errmsg", DI_TYPE_OBJECT, errmsg_getter);
	di_unref_object(errmsg_getter);

	err->dtor = di_free_error;
	return (void *)err;
}

int di_gmethod(struct di_object *o, const char *name, void (*fn)(void)) {
	with_object_cleanup(di_object) m = di_new_object_with_type(struct di_object);
	((struct di_object_internal *)m)->call = (void *)fn;

	return di_add_member_clone(o, name, DI_TYPE_OBJECT, m);
}

static int _emit_proxied_signal(struct di_object *o, di_type_t *rt, union di_value *ret,
                                struct di_tuple t) {
	char *signal = NULL;
	struct di_weak_object *weak = NULL;
	di_get(o, "___new_signal_name", signal);
	di_get(o, "__proxy_object", weak);

	di_object_with_cleanup proxy = di_upgrade_weak_ref(weak);
	if (proxy) {
		di_emitn(proxy, signal, t);
	}

	free(signal);
	return 0;
}

static void _del_proxied_signal(struct di_weak_object *weak, char *signal) {
	auto proxy = di_upgrade_weak_ref(weak);
	if (proxy) {
		char *buf;
		asprintf(&buf, "__proxy_%s_listen_handle", signal);
		di_remove_member_raw(proxy, buf);
		free(buf);
	}
}

// Add a listener to src for "srcsig". When "srcsig" is emitted, the proxy will emit
// "proxysig" on the proxy object. The listen handle to the source signal is automatically
// kept alive in the proxy object. And this function register a signal deleter for the
// proxied signal to stop listening to the source signal when all the listener are gone.
//
// This function sets "__del_signal_<proxysig>", "__proxy_<srcsig>_listen_handle" in the
// proxy object.
//
// This function doesn't allow proxying internal signals
//
// @return 0 or error code
int di_proxy_signal(struct di_object *src, const char *srcsig, struct di_object *proxy,
                    const char *proxysig) {
	int ret = 0;
	if (strncmp(srcsig, "__", 2) == 0) {
		return -EPERM;
	}

	auto c = di_new_object_with_type(struct di_object);
	di_member_clone(c, "___new_signal_name", (char *)proxysig);

	auto weak_proxy = di_weakly_ref_object(proxy);
	di_member_clone(c, "__proxy_object", weak_proxy);
	di_set_object_call(c, _emit_proxied_signal);

	auto listen_handle = di_listen_to(src, srcsig, c);
	char *buf;
	asprintf(&buf, "__proxy_%s_listen_handle", proxysig);
	di_member(proxy, buf, listen_handle);
	free(buf);

	asprintf(&buf, "__del_signal_%s", proxysig);
	if (!di_has_member(proxy, buf)) {
		auto cl = (struct di_object *)di_closure(_del_proxied_signal,
		                                         (weak_proxy, (char *)proxysig));
		ret = di_member(proxy, buf, cl);
		if (ret != 0) {
			goto err;
		}
	}

err:
	free(buf);
	return 0;
}
