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

	struct di_string errmsg;
	int ret = vasprintf((char **)&errmsg.data, fmt, ap);
	if (ret < 0) {
		errmsg = di_string_dup(fmt);
	} else {
		errmsg.length = strlen(errmsg.data);
	}

	auto err = di_new_object_with_type(struct di_object);
	di_set_type(err, "deai:Error");

	di_member(err, "errmsg", errmsg);
	return err;
}

static int emit_proxied_signal(struct di_object *o, di_type_t *rt, union di_value *ret,
                               struct di_tuple t) {
	const char *signal = NULL;
	struct di_object *proxy = NULL;
	di_get(o, "new_signal_name", signal);
	di_get(o, "proxy_object", proxy);

	di_emitn(proxy, di_string_borrow(signal), t);
	free((char *)signal);
	return 0;
}

static void del_proxied_signal(struct di_string proxysig, struct di_object *nonnull proxy) {
	with_cleanup(free_charpp) char *listen_handle_name, *event_source_name, *del_signal_name;
	asprintf(&listen_handle_name, "__proxy_%.*s_listen_handle", (int)proxysig.length,
	         proxysig.data);
	asprintf(&event_source_name, "__proxy_%.*s_event_source", (int)proxysig.length,
	         proxysig.data);
	asprintf(&del_signal_name, "__delete___signal_%.*s", (int)proxysig.length,
	         proxysig.data);

	di_remove_member_raw(proxy, di_string_borrow(listen_handle_name));
	di_remove_member_raw(proxy, di_string_borrow(event_source_name));
	di_remove_member_raw(proxy, di_string_borrow(del_signal_name));
}

// Add a listener to src for "srcsig". When "srcsig" is emitted, the proxy will emit
// "proxysig" on the proxy object. The listen handle to the source signal, and the source
// object is automatically kept alive in the proxy object. And this function register a
// signal deleter for the proxied signal to stop listening to the source signal when all
// the listener are gone.
//
// This function sets "__delete___signal_<proxysig>", "__proxy_<srcsig>_listen_handle",
// "__proxy_<srcsig>_event_source" in the proxy object.
//
// This function doesn't allow proxying internal signals
//
// @return 0 or error code
int di_proxy_signal(struct di_object *src, struct di_string srcsig,
                    struct di_object *proxy, struct di_string proxysig) {
	int ret = 0;
	if (srcsig.length >= 2 && strncmp(srcsig.data, "__", 2) == 0) {
		return -EPERM;
	}

	with_cleanup(free_charpp) char *listen_handle_name, *event_source_name, *del_signal_name;
	asprintf(&listen_handle_name, "__proxy_%.*s_listen_handle", (int)proxysig.length,
	         proxysig.data);
	asprintf(&event_source_name, "__proxy_%.*s_event_source", (int)proxysig.length,
	         proxysig.data);
	asprintf(&del_signal_name, "__delete___signal_%.*s", (int)proxysig.length,
	         proxysig.data);
	if (di_has_member(proxy, listen_handle_name) ||
	    di_has_member(proxy, event_source_name) || di_has_member(proxy, del_signal_name)) {
		return -EEXIST;
	}

	di_object_with_cleanup c = di_new_object_with_type(struct di_object);
	di_member_clone(c, "new_signal_name", proxysig);

	di_member_clone(c, "proxy_object", proxy);
	di_set_object_call(c, emit_proxied_signal);

	di_object_with_cleanup listen_handle = di_listen_to(src, srcsig, c);
	struct di_object *auto_listen_handle;
	DI_CHECK_OK(di_callr(listen_handle, "auto_stop", auto_listen_handle));

	di_member(proxy, listen_handle_name, auto_listen_handle);
	di_member_clone(proxy, event_source_name, src);
	auto cl = (struct di_object *)di_closure(del_proxied_signal, (proxysig),
	                                         struct di_object *);
	ret = di_member(proxy, del_signal_name, cl);

	return ret;
}

static int di_redirected_getter_imp(struct di_object *getter, di_type_t *rt,
                                    union di_value *r, struct di_tuple args) {
	// argument should be one object "self".
	if (args.length != 1) {
		return -EINVAL;
	}
	di_weak_object_with_cleanup them = NULL;
	di_string_with_cleanup theirs = DI_STRING_INIT;
	if (di_get(getter, "them", them) != 0) {
		return -ENOENT;
	}
	if (di_get(getter, "theirs", theirs) != 0) {
		return -ENOENT;
	}
	di_object_with_cleanup them_obj = di_upgrade_weak_ref(them);
	if (them_obj == NULL) {
		return -ENOENT;
	}

	return di_getx(them_obj, theirs, rt, r);
}

/// Create a getter that, when called, returns member `theirs` from `them`
struct di_object *di_redirected_getter(struct di_weak_object *them, struct di_string theirs) {
	auto ret = di_new_object_with_type(struct di_object);
	DI_CHECK_OK(di_member_clone(ret, "them", them));
	DI_CHECK_OK(di_member_clone(ret, "theirs", theirs));
	di_set_object_call(ret, di_redirected_getter_imp);

	return ret;
}

static int di_redirected_setter_imp(struct di_object *setter, di_type_t *rt,
                                    union di_value *r, struct di_tuple args) {
	if (args.length != 2) {
		return -EINVAL;
	}
	di_weak_object_with_cleanup them = NULL;
	di_string_with_cleanup theirs = DI_STRING_INIT;
	if (di_get(setter, "them", them) != 0) {
		return -ENOENT;
	}
	if (di_get(setter, "theirs", theirs) != 0) {
		return -ENOENT;
	}
	di_object_with_cleanup them_obj = di_upgrade_weak_ref(them);
	if (them_obj == NULL) {
		return -ENOENT;
	}
	return di_setx(them_obj, theirs, args.elements[1].type, args.elements[1].value);
}
/// Create a setter that, when called, sets member `theirs` of `them` instead
struct di_object *di_redirected_setter(struct di_weak_object *them, struct di_string theirs) {
	auto ret = di_new_object_with_type(struct di_object);
	DI_CHECK_OK(di_member_clone(ret, "them", them));
	DI_CHECK_OK(di_member_clone(ret, "theirs", theirs));
	di_set_object_call(ret, di_redirected_setter_imp);

	return ret;
}

static int di_redirected_signal_setter_imp(struct di_object *setter, di_type_t *rt,
                                           union di_value *r, struct di_tuple args) {
	if (args.length != 2) {
		return -EINVAL;
	}
	if (args.elements[1].type != DI_TYPE_OBJECT) {
		return -EINVAL;
	}
	di_weak_object_with_cleanup them = NULL;
	di_string_with_cleanup theirs = DI_STRING_INIT;
	if (di_get(setter, "them", them) != 0) {
		return -ENOENT;
	}
	if (di_get(setter, "theirs", theirs) != 0) {
		return -ENOENT;
	}
	di_object_with_cleanup them_obj = di_upgrade_weak_ref(them);
	if (them_obj == NULL) {
		return -ENOENT;
	}
	int rc = di_setx(them_obj, theirs, args.elements[1].type, args.elements[1].value);
	if (rc != 0) {
		return rc;
	}

	struct di_object *sig = args.elements[1].value->object;
	rc = di_setx(sig, di_string_borrow_literal("weak_source"), DI_TYPE_WEAK_OBJECT, &them);
	if (rc != 0) {
		return rc;
	}
	return di_setx(sig, di_string_borrow_literal("signal_name"), DI_TYPE_STRING, &theirs);
}
/// Create a setter that, when called, sets member `theirs` of `them` instead. Specialized
/// for setting signal objects, meaning it will update the signal metadata too.
struct di_object *
di_redirected_signal_setter(struct di_weak_object *them, struct di_string theirs) {
	auto ret = di_new_object_with_type(struct di_object);
	DI_CHECK_OK(di_member_clone(ret, "them", them));
	DI_CHECK_OK(di_member_clone(ret, "theirs", theirs));
	di_set_object_call(ret, di_redirected_signal_setter_imp);

	return ret;
}

/// Redirect listener of `ours` on `us` to `theirs` on `them`. Whenever handlers are
/// registered for `ours` on `us`, they will be redirected to `theirs` on `them` instead,
/// by adding a getter/setter for __signal_<ours> on `us`.
int di_redirect_signal(struct di_object *us, struct di_weak_object *them,
                       struct di_string ours, struct di_string theirs) {
	di_string_with_cleanup sig_theirs =
	    di_string_printf("__signal_%.*s", (int)theirs.length, theirs.data);

	di_object_with_cleanup getter = di_redirected_getter(them, sig_theirs);
	di_object_with_cleanup setter = di_redirected_signal_setter(them, sig_theirs);
	di_string_with_cleanup get_ours =
	    di_string_printf("__get___signal_%.*s", (int)ours.length, ours.data);
	di_string_with_cleanup set_ours =
	    di_string_printf("__set___signal_%.*s", (int)ours.length, ours.data);

	int rc = di_add_member_move(us, get_ours, (di_type_t[]){DI_TYPE_OBJECT}, &getter);
	if (rc != 0) {
		return rc;
	}
	rc = di_add_member_move(us, set_ours, (di_type_t[]){DI_TYPE_OBJECT}, &setter);
	return rc;
}
