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

static int di_redirected_getter_imp(struct di_object *getter, di_type *rt,
                                    union di_value *r, struct di_tuple args) {
	// argument should be one object "self".
	if (args.length != 1) {
		return -EINVAL;
	}
	scoped_di_weak_object *them = NULL;
	scoped_di_string theirs = DI_STRING_INIT;
	if (di_get(getter, "them", them) != 0) {
		return -ENOENT;
	}
	if (di_get(getter, "theirs", theirs) != 0) {
		return -ENOENT;
	}
	scoped_di_object *them_obj = di_upgrade_weak_ref(them);
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

static int di_redirected_setter_imp(struct di_object *setter, di_type *rt,
                                    union di_value *r, struct di_tuple args) {
	if (args.length != 2) {
		return -EINVAL;
	}
	scoped_di_weak_object *them = NULL;
	scoped_di_string theirs = DI_STRING_INIT;
	if (di_get(setter, "them", them) != 0) {
		return -ENOENT;
	}
	if (di_get(setter, "theirs", theirs) != 0) {
		return -ENOENT;
	}
	scoped_di_object *them_obj = di_upgrade_weak_ref(them);
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

static int di_redirected_signal_setter_imp(struct di_object *setter, di_type *rt,
                                           union di_value *r, struct di_tuple args) {
	if (args.length != 2) {
		return -EINVAL;
	}
	if (args.elements[1].type != DI_TYPE_OBJECT) {
		return -EINVAL;
	}
	scoped_di_weak_object *them = NULL;
	scoped_di_string theirs = DI_STRING_INIT;
	if (di_get(setter, "them", them) != 0) {
		return -ENOENT;
	}
	if (di_get(setter, "theirs", theirs) != 0) {
		return -ENOENT;
	}
	scoped_di_object *them_obj = di_upgrade_weak_ref(them);
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
	scoped_di_string sig_theirs =
	    di_string_printf("__signal_%.*s", (int)theirs.length, theirs.data);

	scoped_di_object *getter = di_redirected_getter(them, sig_theirs);
	scoped_di_object *setter = di_redirected_signal_setter(them, sig_theirs);
	scoped_di_string get_ours =
	    di_string_printf("__get___signal_%.*s", (int)ours.length, ours.data);
	scoped_di_string set_ours =
	    di_string_printf("__set___signal_%.*s", (int)ours.length, ours.data);

	int rc = di_add_member_move(us, get_ours, (di_type[]){DI_TYPE_OBJECT}, &getter);
	if (rc != 0) {
		return rc;
	}
	rc = di_add_member_move(us, set_ours, (di_type[]){DI_TYPE_OBJECT}, &setter);
	return rc;
}
