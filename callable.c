/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <assert.h>
#include <stdarg.h>

#include <deai/callable.h>
#include <deai/object.h>
#include <deai/type.h>

#include "di_internal.h"
#include "utils.h"

struct di_closure {
	di_object_internal;
	void (*nonnull fn)(void);

	/// Number of actual arguments
	int nargs;
	/// Number of captured values
	int nargs0;
	/// Return type
	di_type rtype;
	ffi_cif cif;
	/// Expected types of the arguments
	di_type atypes[];
};

static_assert(sizeof(di_value) >= sizeof(ffi_arg), "ffi_arg is too big");

struct ffi_call_args {
	ffi_cif *cif;
	void (*fn)(void);
	void *ret;
	void *xargs;
};

static void di_call_ffi_call(void *args_) {
	struct ffi_call_args *args = args_;
	ffi_call(args->cif, args->fn, args->ret, args->xargs);
}

static int _di_typed_trampoline(ffi_cif *cif, void (*fn)(void), void *ret,
                                const di_type *fnats, di_tuple args0, di_tuple args) {
	assert(args.length == 0 || args.elements != NULL);
	assert(args0.length == 0 || args0.elements != NULL);
	assert(args.length >= 0 && args0.length >= 0);
	assert(args.length + args0.length <= MAX_NARGS);

	struct di_variant *vars = args.elements;
	di_value **xargs = alloca((args0.length + args.length) * sizeof(void *));
	if (args0.elements != 0) {
		for (int i = 0; i < args0.length; i++) {
			xargs[i] = args0.elements[i].value;
		}
	}
	memset(xargs + args0.length, 0, sizeof(void *) * args.length);

	int rc = 0;
	for (int i = args0.length; i < args0.length + args.length; i++) {
		// Type check and implicit conversion
		// conversion between all types of integers are allowed
		// as long as there's no overflow
		xargs[i] = alloca(di_sizeof_type(fnats[i - args0.length]));
		rc = di_type_conversion(vars[i - args0.length].type,
		                        vars[i - args0.length].value,
		                        fnats[i - args0.length], xargs[i], true);
		if (rc != 0) {
			// Conversion failed
			return rc;
		}
	}

	struct ffi_call_args ffi_args = {
	    .cif = cif,
	    .fn = fn,
	    .ret = ret,
	    .xargs = (void *)xargs,
	};

	di_object *errobj = di_try(di_call_ffi_call, &ffi_args);
	if (errobj != NULL) {
		fprintf(stderr, "Caught error from di closure, it says:\n");
		di_string err = DI_STRING_INIT;
		DI_CHECK_OK(di_get(errobj, "errmsg", err));
		fprintf(stderr, "%.*s\n", (int)err.length, err.data);
		di_free_string(err);
		di_unref_object(errobj);
	}
	return rc;
}

static int closure_trampoline(di_object *o, di_type *rtype, di_value *ret, di_tuple t) {
	if (!di_check_type(o, "deai:closure")) {
		return -EINVAL;
	}

	struct di_closure *cl = (void *)o;
	if (t.length != cl->nargs) {
		return -EINVAL;
	}

	*rtype = cl->rtype;

	di_tuple captures;
	DI_CHECK_OK(di_get(o, "captures", captures));
	int rc =
	    _di_typed_trampoline(&cl->cif, cl->fn, ret, cl->atypes + cl->nargs0, captures, t);
	di_free_tuple(captures);
	return rc;
}

static void free_closure(di_object *o) {
	assert(di_check_type(o, "deai:closure"));

	struct di_closure *cl = (void *)o;
	free(cl->cif.arg_types);
}

struct di_closure *di_create_closure(void (*fn)(void), di_type rtype, di_tuple captures,
                                     int nargs, const di_type *arg_types) {
	if (nargs < 0 || captures.length + nargs > MAX_NARGS) {
		return ERR_PTR(-E2BIG);
	}

	for (int i = 0; i < captures.length; i++) {
		if (di_sizeof_type(captures.elements[i].type) == 0) {
			return ERR_PTR(-EINVAL);
		}
	}

	for (int i = 0; i < nargs; i++) {
		if (di_sizeof_type(arg_types[i]) == 0) {
			return ERR_PTR(-EINVAL);
		}
	}

	struct di_closure *cl = (void *)di_new_object(
	    sizeof(struct di_closure) + sizeof(di_type) * (captures.length + nargs),
	    alignof(struct di_closure));

	cl->rtype = rtype;
	cl->call = closure_trampoline;
	cl->fn = fn;
	cl->dtor = free_closure;
	cl->nargs = nargs;
	cl->nargs0 = captures.length;

	if (captures.length) {
		for (int i = 0; i < captures.length; i++) {
			cl->atypes[i] = captures.elements[i].type;
		}
	}
	if (nargs) {
		memcpy(cl->atypes + captures.length, arg_types, sizeof(di_type) * nargs);
	}

	auto ffiret = di_ffi_prep_cif(&cl->cif, captures.length + nargs, cl->rtype, cl->atypes);
	if (ffiret != FFI_OK) {
		free(cl);
		return ERR_PTR(-EINVAL);
	}

	di_member_clone(cl, "captures", captures);
	DI_OK_OR_RET_PTR(di_set_type((void *)cl, "deai:closure"));

	return cl;
}

int di_add_method(di_object *o, di_string name, void (*fn)(void), di_type rtype, int nargs, ...) {
	if (nargs < 0 || nargs + 1 > MAX_NARGS) {
		return -EINVAL;
	}

	// Get argument types
	di_type *ats = alloca(sizeof(di_type) * (nargs + 1));
	va_list ap;
	va_start(ap, nargs);
	for (unsigned int i = 0; i < nargs; i++) {
		ats[i + 1] = va_arg(ap, di_type);
		if (ats[i + 1] == DI_TYPE_NIL) {
			return -EINVAL;
		}
	}
	va_end(ap);

	ats[0] = DI_TYPE_OBJECT;
	auto f = di_create_closure(fn, rtype, DI_TUPLE_INIT, nargs + 1, ats);
	return di_add_member_move(o, name, (di_type[]){DI_TYPE_OBJECT}, (void **)&f);
}

// va_args version of di_call_callable
int di_call_objectv(di_object *_obj, di_type *rtype, di_value *ret, va_list ap) {
	auto obj = (di_object_internal *)_obj;
	if (!obj->call) {
		return -EINVAL;
	}

	va_list ap2;
	di_tuple tu = DI_TUPLE_INIT;

	va_copy(ap2, ap);
	di_type t = va_arg(ap, di_type);
	while (t < DI_LAST_TYPE) {
		if (di_sizeof_type(t) == 0) {
			va_end(ap2);
			return -EINVAL;
		}
		tu.length++;
		va_arg_with_di_type(ap, t, NULL);
		t = va_arg(ap, di_type);
	}

	if (tu.length > 0) {
		tu.elements = alloca(sizeof(struct di_variant) * tu.length);
		for (unsigned int i = 0; i < tu.length; i++) {
			tu.elements[i].type = va_arg(ap2, di_type);
			assert(di_sizeof_type(tu.elements[i].type) != 0);
			tu.elements[i].value = alloca(di_sizeof_type(tu.elements[i].type));
			va_arg_with_di_type(ap2, tu.elements[i].type, tu.elements[i].value);
		}
		va_end(ap2);
	}

	return obj->call(_obj, rtype, ret, tu);
}

struct di_field_getter {
	di_object_internal;
	di_type type;
	ptrdiff_t offset;
};

static int
di_field_getter_call(di_object *getter, di_type *rtype, di_value *ret, di_tuple args) {
	DI_CHECK(di_check_type(getter, "deai:FieldGetter"));

	if (args.elements[0].type != DI_TYPE_OBJECT) {
		DI_ASSERT(false, "first argument to getter is not an object");
		return -EINVAL;
	}

	auto object = (char *)args.elements[0].value->object;
	auto field_getter = (struct di_field_getter *)getter;
	*rtype = field_getter->type;

	di_copy_value(field_getter->type, ret, object + field_getter->offset);
	return 0;
}

di_object *di_new_field_getter(di_type type, ptrdiff_t offset) {
	auto ret = di_new_object_with_type(struct di_field_getter);
	auto obj = (di_object *)ret;
	ret->offset = offset;
	ret->type = type;
	ret->call = di_field_getter_call;
	di_set_type(obj, "deai:FieldGetter");
	return obj;
}

int di_call_object(di_object *o, di_type *rtype, di_value *ret, ...) {
	va_list ap;

	va_start(ap, ret);
	return di_call_objectv(o, rtype, ret, ap);
}

int di_call_objectt(di_object *obj, di_type *rt, di_value *ret, di_tuple args) {
	auto internal = (di_object_internal *)obj;
	return internal->call(obj, rt, ret, args);
}
