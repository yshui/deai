/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <assert.h>
#include <stdarg.h>

#include <deai/callable.h>
#include <deai/object.h>
#include <deai/type.h>
#include <deai/helper.h>

#include "di_internal.h"

struct di_raw_closure {
	di_object_internal obj;
	di_call_fn raw_fn;
};

struct di_closure {
	struct di_raw_closure raw;
	void (*nonnull fn)(void);

	/// Number of actual arguments
	int nargs;
	/// Return type
	di_type rtype;
	ffi_cif cif;
	/// Expected types of the arguments
	di_type atypes[];
};

static_assert(sizeof(di_value) >= sizeof(ffi_arg), "ffi_arg is too big");

static int di_typed_trampoline(ffi_cif *cif, void (*fn)(void), di_type *ret_type,
                               void *ret, const di_type *fnats, di_tuple args) {
	assert(args.length == 0 || args.elements != NULL);
	assert(args.length >= 0);
	assert(args.length <= MAX_NARGS);

	struct di_variant *vars = args.elements;
	auto xargs = (di_value **)alloca(args.length * sizeof(void *));

	int rc = 0;
	for (int i = 0; i < args.length; i++) {
		// Type check and implicit conversion
		// conversion between all types of integers are allowed
		// as long as there's no overflow
		xargs[i] = alloca(di_sizeof_type(fnats[i]));
		rc = di_type_conversion(vars[i].type, vars[i].value, fnats[i], xargs[i], true);
		if (rc != 0) {
			// Conversion failed
			return rc;
		}
	}

	ffi_call(cif, fn, ret, (void **)xargs);
	return rc;
}
static const char closure_type[] = "deai:closure";
static int closure_trampoline(di_object *o, di_type *rtype, di_value *ret, di_tuple t) {
	if (!di_check_type(o, closure_type)) {
		return -EINVAL;
	}

	struct di_closure *cl = (void *)o;
	if (t.length != cl->nargs) {
		return -EINVAL;
	}

	*rtype = cl->rtype;
	return di_typed_trampoline(&cl->cif, cl->fn, rtype, ret, cl->atypes, t);
}

static int raw_closure_trampoline(di_object *o, di_type *rtype, di_value *ret, di_tuple t) {
	struct di_raw_closure *cl = (void *)o;

	di_tuple captures = DI_TUPLE_INIT;
	di_rawget_borrowed(o, "captures", captures);

	di_tuple args_with_captures = {
	    .length = captures.length + t.length,
	    .elements = alloca(sizeof(struct di_variant) * (captures.length + t.length)),
	};
	memcpy(args_with_captures.elements, captures.elements,
	       sizeof(struct di_variant) * captures.length);
	memcpy(args_with_captures.elements + captures.length, t.elements,
	       sizeof(struct di_variant) * t.length);
	return cl->raw_fn(o, rtype, ret, args_with_captures);
}

static void free_closure(di_object *o) {
	assert(di_check_type(o, closure_type));

	struct di_closure *cl = (void *)o;
	free((void *)cl->cif.arg_types);
}

/// Create a di_object that when called, calls the given function with the given captures
/// prepended to the arguments
struct di_raw_closure *
di_create_raw_closure(di_call_fn fn, di_tuple captures, size_t size, size_t align) {
	if (captures.length > MAX_NARGS) {
		return ERR_PTR(-E2BIG);
	}

	for (int i = 0; i < captures.length; i++) {
		if (di_sizeof_type(captures.elements[i].type) == 0) {
			return ERR_PTR(-EINVAL);
		}
	}

	struct di_raw_closure *cl = (void *)di_new_object(size, align);
	cl->raw_fn = fn;
	cl->obj.call = raw_closure_trampoline;

	di_member_clone(cl, "captures", captures);
	DI_OK_OR_RET_PTR(di_set_type((void *)cl, "deai:raw_closure"));

	return cl;
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

	struct di_closure *cl = (void *)di_create_raw_closure(
	    closure_trampoline, captures,
	    offsetof(struct di_closure, atypes[captures.length + nargs]),
	    alignof(struct di_closure));

	cl->rtype = rtype;
	cl->fn = fn;
	cl->nargs = nargs + (int)captures.length;

	for (int i = 0; i < captures.length; i++) {
		cl->atypes[i] = captures.elements[i].type;
	}
	if (nargs) {
		memcpy(cl->atypes + captures.length, arg_types, sizeof(di_type) * nargs);
	}

	auto ffiret = di_ffi_prep_cif(&cl->cif, cl->nargs, cl->rtype, cl->atypes);
	if (ffiret != FFI_OK) {
		di_unref_object((di_object *)cl);
		return ERR_PTR(-EINVAL);
	}

	cl->raw.obj.dtor = free_closure;
	DI_OK_OR_RET_PTR(di_set_type((void *)cl, closure_type));

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
	return di_add_member_move(o, name, (di_type[]){DI_TYPE_OBJECT}, (void *)&f);
}

struct di_field_getter {
	di_object_internal;
	di_type type;
	ptrdiff_t offset;
};
static const char field_getter_type[] = "deai:FieldGetter";
static int
di_field_getter_call(di_object *getter, di_type *rtype, di_value *ret, di_tuple args) {
	DI_CHECK(di_check_type(getter, field_getter_type));

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
	di_set_type(obj, field_getter_type);
	return obj;
}

int di_call_object(di_object *obj, di_type *rt, di_value *ret, di_tuple args) {
	auto internal = (di_object_internal *)obj;
	return internal->call(obj, rt, ret, args);
}
