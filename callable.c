/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <assert.h>
#include <stdarg.h>

#include <deai/callable.h>
#include <deai/object.h>

#include "di_internal.h"
#include "utils.h"

static int
di_typed_trampoline(struct di_object *o, di_type_t *rt, void **ret, int nargs,
                    const di_type_t *ats, const void *const *args) {
	struct di_typed_method *fn = (void *)o;

	if (nargs + 1 != fn->nargs)
		return -EINVAL;

	assert(nargs == 0 || args != NULL);

	void *null_ptr = NULL;
	const void **xargs = calloc(nargs + 1, sizeof(void *));
	int rc = 0;
	int start = 0;
	if (fn->this) {
		xargs[0] = &fn->this;
		start = 1;
	}
	for (int i = 0; i < nargs; i++) {
		// Type check and implicit conversion
		// conversion between all types of integers are allowed
		// as long as there's no overflow
		rc = di_type_conversion(ats[i], args[i], fn->atypes[i + start],
		                        xargs + i + start);
		if (rc != 0) {
			if (ats[i] == DI_TYPE_NIL) {
				struct di_array *arr;
				switch (fn->atypes[i + start]) {
				case DI_TYPE_OBJECT:
					xargs[i + start] = &null_ptr;
					break;
				case DI_TYPE_STRING:
				case DI_TYPE_POINTER:
					xargs[i + start] = &null_ptr;
					break;
				case DI_TYPE_ARRAY:
					arr = tmalloc(struct di_array, 1);
					arr->length = 0;
					arr->elem_type = DI_TYPE_NIL;
					xargs[i + start] = arr;
					break;
				default: rc = -EINVAL; goto out;
				}
			} else {
				rc = -EINVAL;
				goto out;
			}
		}
	}

	if (fn->rtype != DI_TYPE_VOID)
		*ret = malloc(di_min_return_size(di_sizeof_type(fn->rtype)));
	else
		*ret = NULL;

	ffi_call(&fn->cif, fn->real_fn_ptr, *ret, (void *)xargs);
	*rt = fn->rtype;

out:
	for (int i = 0; i < nargs; i++)
		if (xargs[i + 1] != args[i] && xargs[i + 1] != &null_ptr)
			free((void *)xargs[i + 1]);
	free(xargs);

	return rc;
}

PUBLIC struct di_object *
di_create_typed_fn(di_fn_t fn, di_type_t rtype, int nargs, ...) {
	struct di_typed_method *f = (void *)di_new_object(
	    sizeof(struct di_typed_method) + sizeof(di_type_t) * (1 + nargs));

	f->rtype = rtype;
	f->call = di_typed_trampoline;
	f->real_fn_ptr = fn;

	va_list ap;
	va_start(ap, nargs);
	for (unsigned int i = 0; i < nargs; i++) {
		f->atypes[i + 1] = va_arg(ap, di_type_t);
		if (f->atypes[i + 1] == DI_TYPE_NIL) {
			free(f);
			return ERR_PTR(-EINVAL);
		}
	}
	va_end(ap);

	f->atypes[0] = DI_TYPE_OBJECT;
	f->nargs = nargs + 1;

	ffi_status ret = di_ffi_prep_cif(&f->cif, nargs + 1, f->rtype, f->atypes);

	if (ret != FFI_OK) {
		free(f);
		return ERR_PTR(-EINVAL);
	}

	di_set_type((void *)f, "function");
	return (void *)f;
}

PUBLIC int
di_add_method(struct di_object *o, const char *name, struct di_object *_fn) {
	if (!di_check_type(_fn, "function"))
		return -EINVAL;

	struct di_typed_method *fn = (void *)_fn;
	if (!fn->call)
		return -EINVAL;

	fn->this = o;
	return di_add_value_member(o, name, false, DI_TYPE_OBJECT, fn);
}

PUBLIC void di_set_this(struct di_object *o, struct di_object *th) {
	if (!di_check_type(o, "function"))
		return;

	struct di_typed_method *fn = (void *)o;
	di_ref_object(th);
	fn->this = th;
}

// va_args version of di_call_callable
PUBLIC int
di_call_objectv(struct di_object *o, di_type_t *rtype, void **ret, va_list ap) {
	if (!o->call)
		return -EINVAL;

	va_list ap2;
	void **args = NULL;
	di_type_t *ats = NULL;

	va_copy(ap2, ap);
	unsigned int nargs = 0;
	di_type_t t = va_arg(ap, di_type_t);
	while (t < DI_LAST_TYPE) {
		if (di_sizeof_type(t) == 0) {
			va_end(ap2);
			return -EINVAL;
		}
		nargs++;
		va_arg_with_di_type(ap, t, NULL);
		t = va_arg(ap, di_type_t);
	}

	if (nargs > 0) {
		args = alloca(sizeof(void *) * nargs);
		ats = alloca(sizeof(di_type_t) * nargs);
		for (unsigned int i = 0; i < nargs; i++) {
			ats[i] = va_arg(ap2, di_type_t);
			assert(di_sizeof_type(ats[i]) != 0);
			args[i] = alloca(di_sizeof_type(ats[i]));
			va_arg_with_di_type(ap2, ats[i], args[i]);
		}
		va_end(ap2);
	}

	return o->call(o, rtype, ret, nargs, ats, (const void *const *)args);
}

PUBLIC int di_call_object(struct di_object *o, di_type_t *rtype, void **ret, ...) {
	va_list ap;

	va_start(ap, ret);
	return di_call_objectv(o, rtype, ret, ap);
}
