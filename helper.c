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

struct di_object *di_new_error(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);

	char *errmsg;
	int ret = vasprintf(&errmsg, fmt, ap);
	if (ret < 0) {
		errmsg = strdup(fmt);
	}

	auto err = di_new_object_with_type(struct di_object);
	di_set_type(err, "deai:Error");
	di_member(err, "errmsg", errmsg);
	return err;
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

static void _del_proxied_signal(char *signal, struct di_object *nonnull proxy) {
	char *buf;
	asprintf(&buf, "__proxy_%s_listen_handle", signal);
	di_remove_member_raw(proxy, buf);
	free(buf);

	asprintf(&buf, "__proxy_%s_event_source", signal);
	di_remove_member_raw(proxy, buf);
	free(buf);
}

define_trivial_cleanup_t(char);
// Add a listener to src for "srcsig". When "srcsig" is emitted, the proxy will emit
// "proxysig" on the proxy object. The listen handle to the source signal, and the source
// object is automatically kept alive in the proxy object. And this function register a
// signal deleter for the proxied signal to stop listening to the source signal when all
// the listener are gone.
//
// This function sets "__del_signal_<proxysig>", "__proxy_<srcsig>_listen_handle",
// "__proxy_<srcsig>_event_source" in the proxy object.
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

	with_cleanup(free_charpp) char *listen_handle_name, *event_source_name, *del_signal_name;
	asprintf(&listen_handle_name, "__proxy_%s_listen_handle", proxysig);
	asprintf(&event_source_name, "__proxy_%s_event_source", proxysig);
	asprintf(&del_signal_name, "__del_signal_%s", proxysig);
	if (di_has_member(proxy, listen_handle_name) ||
	    di_has_member(proxy, event_source_name) || di_has_member(proxy, del_signal_name)) {
		return -EEXIST;
	}

	di_object_with_cleanup c = di_new_object_with_type(struct di_object);
	di_member_clone(c, "___new_signal_name", (char *)proxysig);

	auto weak_proxy = di_weakly_ref_object(proxy);
	di_member_clone(c, "__proxy_object", weak_proxy);
	di_set_object_call(c, _emit_proxied_signal);

	auto listen_handle = di_listen_to(src, srcsig, c);

	di_member(proxy, listen_handle_name, listen_handle);
	di_member_clone(proxy, event_source_name, src);
	auto cl = (struct di_object *)di_closure(_del_proxied_signal, ((char *)proxysig),
	                                         struct di_object *);
	ret = di_member(proxy, del_signal_name, cl);

	return ret;
}
