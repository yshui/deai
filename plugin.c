/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#define _GNU_SOURCE
#include <assert.h>
#include <deai.h>
#include <helper.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/mman.h>

#include "config.h"
#include "di_internal.h"
#include "utils.h"

#ifdef DI_REFCOUNT_DEBUG
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#endif

#define PUBLIC __attribute__((visibility("default")))

const void *null_ptr = NULL;

struct di_object_internal {
	struct di_method_internal *fn;
	struct di_signal *sd;

	uint64_t ref_count;
	uint8_t destroyed;
};

PUBLIC struct di_module *di_find_module(struct deai *p, const char *mod_name) {
	struct di_module_internal *m = NULL;
	HASH_FIND_STR(p->m, mod_name, m);
	if (m)
		di_ref_object((void *)m);
	return (void *)m;
}

PUBLIC struct di_module *di_get_modules(struct deai *p) {
	di_ref_object((void *)p->m);
	return (void *)p->m;
}

PUBLIC struct di_module *di_next_module(struct di_module *pm) {
	struct di_module_internal *m = (void *)pm;
	struct di_module *nm = m->hh.next;
	if (nm)
		di_ref_object((void *)nm);
	di_unref_object((void *)&m);
	return nm;
}

PUBLIC struct di_method *di_find_method(struct di_object *pm, const char *fn_name) {
	struct di_method_internal *fn = NULL;
	HASH_FIND_STR((struct di_method_internal *)pm->fn, fn_name, fn);
	return (void *)fn;
}

PUBLIC struct di_method *di_get_methods(struct di_object *pm) {
	return (struct di_method *)pm->fn;
}

PUBLIC struct di_method *di_next_method(struct di_method *fn) {
	return ((struct di_method_internal *)fn)->hh.next;
}

PUBLIC struct di_object *di_new_object(size_t sz) {
	if (sz < sizeof(struct di_object))
		return NULL;

	struct di_object *obj = calloc(1, sz);
	obj->evd = NULL;
	obj->fn = NULL;
	obj->ref_count = 1;
	return obj;
}

static void di_free_error(struct di_object *o) {
	struct di_error *e = (void *)o;
	free(e->msg);
}

static const char *di_get_error_msg(struct di_object *o) {
	struct di_error *e = (void *)o;
	return strdup(e->msg);
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

	di_register_typed_method((void *)err, (void *)di_free_error, "__dtor",
	                         DI_TYPE_VOID, 0);

	di_register_typed_method((void *)err, (void *)di_get_error_msg,
	                         "__get_error_msg", DI_TYPE_STRING, 0);
	return (void *)err;
}

static void di_free_module(struct di_module *_m) {
	struct di_module_internal *m = (void *)_m;
	struct di_method_internal *mdtor =
	    (void *)di_find_method((void *)m, "__module_dtor");
	if (mdtor) {
		di_type_t rtype;
		void *ret;
		auto status =
		    di_call_callable_v((void *)mdtor, &rtype, &ret, DI_LAST_TYPE);

		// TODO check status
		// assert(rtype == DI_TYPE_VOID);
		(void)status;
	}
	// fprintf(stderr, "Free module %s\n", m->name);
	free(m->name);
}

PUBLIC struct di_module *di_new_module(const char *name, size_t size) {
	if (size < sizeof(struct di_module))
		return NULL;

	struct di_module_internal *pm = (void *)di_new_object(size);
	pm->name = strdup(name);

	di_register_typed_method((void *)pm, (di_fn_t)di_free_module, "__dtor",
	                         DI_TYPE_VOID, 0);

	return (void *)pm;
}

PUBLIC void di_call_typed_method(struct di_typed_method *fn, void *ret, ...) {
	void **args = alloca(sizeof(void *) * (fn->nargs + 1));
	va_list ap;
	va_start(ap, ret);
	for (unsigned int i = 0; i < fn->nargs; i++)
		args[i] = va_arg(ap, void *);
	va_end(ap);
	args[0] = alloca(sizeof(void *));
	*(void **)args[0] = fn->this;
	ffi_call(&fn->cif, fn->real_fn_ptr, ret, args);
}

PUBLIC int di_call_callable(struct di_callable *c, di_type_t *rtype, void **ret,
                            unsigned int nargs, const di_type_t *atypes,
                            const void *const *args) {
	*ret = NULL;
	return c->fn_ptr(rtype, ret, nargs, atypes, args, c);
}

static inline void di_va_arg_with_di_type(va_list ap, di_type_t t, void *buf) {
	void *ptr, *src = NULL;
	int64_t i64;
	uint64_t u64;
	int ni;
	unsigned int nui;
	double d;

	switch (t) {
	case DI_TYPE_STRING:
	case DI_TYPE_POINTER:
	case DI_TYPE_OBJECT:
		ptr = va_arg(ap, void *);
		src = &ptr;
		break;
	case DI_TYPE_NINT:
		ni = va_arg(ap, int);
		src = &ni;
		break;
	case DI_TYPE_NUINT:
		nui = va_arg(ap, unsigned int);
		src = &nui;
		break;
	case DI_TYPE_INT:
		i64 = va_arg(ap, int64_t);
		src = &i64;
		break;
	case DI_TYPE_UINT:
		u64 = va_arg(ap, uint64_t);
		src = &u64;
		break;
	case DI_TYPE_FLOAT:
		d = va_arg(ap, double);
		src = &d;
		break;
	default: assert(0);
	}

	// if buf == NULL, the caller just want to pop the value
	if (buf)
		memcpy(buf, src, di_sizeof_type(t));
}

// va_args version of di_call_callable
PUBLIC int
di_call_callable_v(struct di_callable *c, di_type_t *rtype, void **ret, ...) {
	va_list ap;
	void **args = NULL;
	di_type_t *ats = NULL;

	va_start(ap, ret);
	unsigned int nargs = 0;
	di_type_t t = va_arg(ap, di_type_t);
	while (t < DI_LAST_TYPE) {
		nargs++;
		di_va_arg_with_di_type(ap, t, NULL);
		t = va_arg(ap, di_type_t);
	}

	va_end(ap);
	if (nargs > 0) {
		args = alloca(sizeof(void *) * nargs);
		ats = alloca(sizeof(di_type_t) * nargs);
		va_start(ap, ret);
		for (unsigned int i = 0; i < nargs; i++) {
			ats[i] = va_arg(ap, di_type_t);
			args[i] = alloca(di_sizeof_type(ats[i]));
			di_va_arg_with_di_type(ap, ats[i], args[i]);
		}
		va_end(ap);
	}

	return di_call_callable(c, rtype, ret, nargs, ats, (const void *const *)args);
}

static void di_free_signal(struct di_object_internal *o, struct di_signal *sig) {
	struct di_listener *l, *ln;
	list_for_each_entry_safe(l, ln, &sig->listeners, siblings) {
		list_del(&l->siblings);
		if (l->ud_free)
			l->ud_free(&l->ud);
		free(l);
	}

	char *buf;
	asprintf(&buf, "__del_listener_%s", sig->name);
	auto m = di_find_method((void *)o, buf);
	free(buf);
	if (m) {
		di_type_t rtype;
		void *ret;
		di_call_callable_v((void *)m, &rtype, &ret, DI_LAST_TYPE);
		di_free_value(rtype, ret);
	}
	free(sig->name);
	free(sig->cif.arg_types);
	free(sig);
}

// Must be called holding external references. i.e.
// __dtor shouldn't cause the reference count to drop to 0
PUBLIC void di_destroy_object(struct di_object *_obj) {
	struct di_object_internal *obj = (void *)_obj;
	assert(obj->destroyed != 2);

	if (obj->destroyed)
		return;

	obj->destroyed = 2;

	auto sig = obj->sd;
	while (sig) {
		auto next_sig = sig->hh.next;
		HASH_DEL(obj->sd, sig);
		di_free_signal(obj, sig);
		sig = next_sig;
	}

	struct di_method_internal *fn = (void *)di_find_method(_obj, "__dtor");
	if (fn) {
		di_type_t rtype;
		void *ret;
		auto status =
		    di_call_callable_v((void *)fn, &rtype, &ret, DI_LAST_TYPE);

		// TODO check status
		// assert(rtype == DI_TYPE_VOID);
		(void)status;
	}
	fn = (void *)obj->fn;
	while (fn) {
		auto next_fn = di_next_method((void *)fn);
		HASH_DEL(obj->fn, fn);
		if (fn->free)
			fn->free(fn);
		else
			free(fn);
		fn = (void *)next_fn;
	}

	obj->destroyed = 1;
}

PUBLIC void di_ref_object(struct di_object *obj) {
	obj->ref_count++;
}

#ifdef DI_REFCOUNT_DEBUG
struct di_dead_object {
	struct list_head sibling;
	uint64_t ref_count;

	void *stack_trace;
};

struct list_head dead_objects = LIST_HEAD_INIT(dead_objects);

static_assert(sizeof(struct di_dead_object) <= sizeof(struct di_object),
              "dead_object too big");
#endif

PUBLIC void di_unref_object(struct di_object **objp) {
	struct di_object *obj = *objp;
	assert(obj->ref_count > 0);
	obj->ref_count--;
	if (obj->ref_count == 0) {
		di_destroy_object(obj);
		free(obj);
	}
	*objp = NULL;
}

// Register a module. This method consume the reference
PUBLIC int di_register_module(struct deai *p, struct di_module *_m) {
	struct di_module_internal *m = (void *)_m;
	struct di_module_internal *old_m = NULL;
	HASH_FIND_STR(p->m, m->name, old_m);
	if (old_m)
		return -EEXIST;
	HASH_ADD_KEYPTR(hh, p->m, m->name, strlen(m->name), m);

	di_emit_signal_v((void *)p, "new-module", m->name);
	return 0;
}

PUBLIC size_t di_min_return_size(size_t in) {
	if (in < sizeof(ffi_arg))
		return sizeof(ffi_arg);
	return in;
}

static int
di_typed_trampoline(di_type_t *rt, void **ret, unsigned int nargs,
                    const di_type_t *ats, const void *const *args, void *ud) {
	struct di_typed_method *fn = ud;

	if (nargs + 1 != fn->nargs)
		return -EINVAL;

	assert(nargs == 0 || args != NULL);

	const void **xargs = calloc(nargs + 1, sizeof(void *));
	int rc = 0;
	xargs[0] = &fn->this;
	for (int i = 0; i < nargs; i++) {
		// Type check and implicit conversion
		// conversion between all types of integers are allowed
		// as long as there's no overflow
		int rc = number_conversion(ats[i], args[i], fn->atypes[i + 1],
		                           xargs + i + 1);
		if (rc != 0) {
			if (ats[i] == DI_TYPE_NIL) {
				struct di_array *arr;
				switch (fn->atypes[i + 1]) {
				case DI_TYPE_OBJECT: xargs[i + 1] = &null_ptr; break;
				case DI_TYPE_STRING:
				case DI_TYPE_POINTER:
					xargs[i + 1] = &null_ptr;
					break;
				case DI_TYPE_ARRAY:
					arr = tmalloc(struct di_array, 1);
					arr->length = 0;
					arr->elem_type = DI_TYPE_NIL;
					xargs[i + 1] = arr;
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

static int
di_untyped_trampoline(di_type_t *rt, void **ret, unsigned int nargs,
                      const di_type_t *ats, const void *const *args, void *ud) {
	struct di_untyped_method *gfn = ud;
	return gfn->real_fn_ptr(rt, ret, nargs, ats, args, gfn->user_data);
}

static void di_free_typed_method(struct di_typed_method *tm) {
	free(tm->name);
	free(tm->cif.arg_types);
	free(tm);
}

PUBLIC int
di_register_typed_method(struct di_object *obj, void (*fn)(void), const char *name,
                         di_type_t rtype, unsigned int nargs, ...) {
	auto f = (struct di_typed_method *)calloc(
	    1, sizeof(struct di_typed_method) + sizeof(di_type_t) * (1 + nargs));
	if (!f)
		return -ENOMEM;

	f->rtype = rtype;
	f->fn_ptr = di_typed_trampoline;
	f->real_fn_ptr = fn;
	f->name = strdup(name);

	f->free = (void *)di_free_typed_method;

	va_list ap;
	va_start(ap, nargs);
	for (unsigned int i = 0; i < nargs; i++) {
		f->atypes[i + 1] = va_arg(ap, di_type_t);
		if (f->atypes[i + 1] == DI_TYPE_NIL) {
			free(f);
			return -EINVAL;
		}
	}
	va_end(ap);

	f->atypes[0] = DI_TYPE_OBJECT;
	f->nargs = nargs + 1;

	ffi_status ret = di_ffi_prep_cif(&f->cif, nargs + 1, f->rtype, f->atypes);

	if (ret != FFI_OK) {
		free(f);
		return -EINVAL;
	}

	f->this = obj;
	return di_register_method(obj, (void *)f);
}

static void di_free_untyped_method(struct di_untyped_method *m) {
	free(m->name);
	if (m->ud_free)
		m->ud_free(&m->user_data);
	free(m);
}

PUBLIC struct di_untyped_method *
di_create_untyped_method(di_callbale_t fn, const char *name, void *user_data,
                         free_fn_t ud_free) {
	auto gfn = tmalloc(struct di_untyped_method, 1);
	gfn->user_data = user_data;
	gfn->real_fn_ptr = fn;
	gfn->fn_ptr = di_untyped_trampoline;
	gfn->name = strdup(name);
	gfn->free = (void *)di_free_untyped_method;
	gfn->ud_free = ud_free;

	return gfn;
}

PUBLIC void di_unregister_method(struct di_object *obj, struct di_method *f) {
	struct di_object_internal *o = (void *)obj;
	struct di_method_internal *m = (void *)f;
	HASH_DEL(o->fn, m);
}

PUBLIC int di_register_method(struct di_object *_obj, struct di_method *f) {
	struct di_object_internal *obj = (void *)_obj;
	struct di_method_internal *old_f;
	if (!f->name)
		return -EINVAL;        // Can't register unnamed function

	HASH_FIND_STR(obj->fn, f->name, old_f);
	if (old_f)
		return -EEXIST;

	HASH_ADD_KEYPTR(hh, obj->fn, f->name, strlen(f->name),
	                (struct di_method_internal *)f);
	return 0;
}

static inline void free_evd(struct di_signal **evd) {
	if (*evd) {
		if ((*evd)->name)
			free((*evd)->name);
		free(*evd);
	}
}

PUBLIC int di_register_signal(struct di_object *r, const char *name, int nargs, ...) {
	with_cleanup(free_evd) struct di_signal *evd = NULL;
	HASH_FIND_STR(r->evd, name, evd);
	if (evd)
		return -EEXIST;

	evd = calloc(1, sizeof(struct di_signal) + sizeof(di_type_t) * (nargs + 1));
	if (!evd)
		return -ENOMEM;

	evd->name = strdup(name);
	if (!evd->name)
		return -ENOMEM;

	evd->nargs = nargs;
	INIT_LIST_HEAD(&evd->listeners);

	va_list ap;
	va_start(ap, nargs);
	for (int i = 0; i < nargs; i++) {
		evd->types[i + 1] = va_arg(ap, di_type_t);
		if (evd->types[i + 1] == DI_TYPE_NIL)
			return -EINVAL;
	}
	va_end(ap);
	evd->types[0] = DI_TYPE_POINTER;

	ffi_status ret =
	    di_ffi_prep_cif(&evd->cif, evd->nargs + 1, DI_TYPE_VOID, evd->types);

	if (ret != FFI_OK)
		return -EINVAL;

	HASH_ADD_KEYPTR(hh, r->evd, evd->name, strlen(evd->name), evd);
	evd = NULL;
	return 0;
}

struct di_typed_listener_data {
	di_fn_t f;
	void *ud;
	free_fn_t ud_free;
};

static void di_typed_listener_trampoline(struct di_signal *sig,
                                         struct di_listener *l, void **ev_data) {
	struct di_typed_listener_data *tl = l->ud;
	void **tmp_data = alloca(sizeof(void *) * (sig->nargs + 1));
	if (ev_data)
		memcpy(tmp_data + 1, ev_data, sizeof(void *) * sig->nargs);

	tmp_data[0] = &tl->ud;
	ffi_call(&sig->cif, tl->f, NULL, tmp_data);
}

PUBLIC const di_type_t *
di_get_signal_arg_types(struct di_signal *sig, unsigned int *nargs) {
	*nargs = sig->nargs;
	return sig->types + 1;
}

PUBLIC struct di_listener *
di_add_untyped_listener(struct di_object *obj, const char *name, void *ud,
                        free_fn_t ud_free, di_listener_fn_t f) {
	struct di_signal *evd = NULL;
	struct di_method *m;
	di_type_t rtype;
	void *ret;
	HASH_FIND_STR(obj->evd, name, evd);
	if (!evd) {
		m = di_find_method(obj, "__add_listener");
		if (m == NULL)
			return ERR_PTR(-ENOENT);

		di_call_callable_v((void *)m, &rtype, &ret, DI_TYPE_STRING, name,
		                   DI_LAST_TYPE);
		di_free_value(rtype, ret);
		HASH_FIND_STR(obj->evd, name, evd);
		if (!evd)
			return ERR_PTR(-ENOENT);
	}

	struct di_listener *l = tmalloc(struct di_listener, 1);
	if (!l)
		return ERR_PTR(-ENOMEM);

	if (list_empty(&evd->listeners)) {
		char *buf;
		asprintf(&buf, "__add_listener_%s", name);
		m = di_find_method(obj, buf);
		free(buf);
		if (m) {
			di_call_callable_v((void *)m, &rtype, &ret, DI_LAST_TYPE);
			di_free_value(rtype, ret);
		}
	}

	l->f = f;
	l->ud = ud;
	l->ud_free = ud_free;
	list_add(&l->siblings, &evd->listeners);

	// Object shouldn't be freed when it has listener
	di_ref_object(obj);
	return l;
}

static void typed_listener_data_free(void **ptr) {
	struct di_typed_listener_data *tl = *ptr;
	assert(tl);
	tl->ud_free(&tl->ud);
	free(tl);
}

PUBLIC struct di_listener *
di_add_typed_listener(struct di_object *obj, const char *name, void *ud,
                      free_fn_t ud_free, di_fn_t f) {
	auto tl = tmalloc(struct di_typed_listener_data, 1);
	tl->ud = ud;
	tl->f = f;
	tl->ud_free = ud_free;

	auto l = di_add_untyped_listener(obj, name, tl, typed_listener_data_free,
	                                 di_typed_listener_trampoline);
	if (IS_ERR(l))
		return l;

	return l;
}

PUBLIC int
di_remove_listener(struct di_object *o, const char *name, struct di_listener *l) {
	struct di_signal *evd = NULL;

	HASH_FIND_STR(o->evd, name, evd);
	if (!evd)
		return -ENOENT;

	struct di_listener *p = NULL;
	list_for_each_entry(p, &evd->listeners, siblings) if (p == l) goto del;
	return -ENOENT;

del:
	list_del(&l->siblings);

	if (list_empty(&evd->listeners)) {
		char *buf;
		asprintf(&buf, "__del_listener_%s", name);
		auto m = di_find_method(o, buf);
		free(buf);
		if (m) {
			di_type_t rtype;
			void *ret;
			di_call_callable_v((void *)m, &rtype, &ret, DI_LAST_TYPE);
			di_free_value(rtype, ret);
		}
	}
	di_unref_object(&o);

	if (l->ud_free)
		l->ud_free(&l->ud);
	free(l);
	return 0;
}

PUBLIC void *di_get_listener_user_data(struct di_listener *l) {
	return l->ud;
}

static int
_di_emit_signal(struct di_object *obj, struct di_signal *evd, void **ev_data) {
	struct di_listener *l, *nl;

	// Hold a reference to prevent object from being freed during
	// signal emission
	assert(obj->ref_count > 0);
	di_ref_object(obj);

	// Allow remove listener from listener
	list_for_each_entry_safe(l, nl, &evd->listeners, siblings) {
		l->f(evd, l, ev_data);
	}

	di_unref_object(&obj);
	return 0;
}

PUBLIC int di_emit_signal(struct di_object *obj, const char *name, void **data) {
	struct di_signal *evd = NULL;
	HASH_FIND_STR(obj->evd, name, evd);
	if (!evd)
		return -ENOENT;

	return _di_emit_signal(obj, evd, data);
}

PUBLIC int di_emit_signal_v(struct di_object *obj, const char *name, ...) {
	struct di_signal *evd = NULL;
	va_list ap;

	HASH_FIND_STR(obj->evd, name, evd);
	if (!evd)
		return -ENOENT;

	void **tmp_data = alloca(sizeof(void *) * evd->nargs);
	va_start(ap, name);
	for (unsigned int i = 0; i < evd->nargs; i++) {
		assert(di_sizeof_type(evd->types[i + 1]) != 0);
		tmp_data[i] = alloca(di_sizeof_type(evd->types[i + 1]));
		di_va_arg_with_di_type(ap, evd->types[i + 1], tmp_data[i]);
	}
	va_end(ap);

	return _di_emit_signal(obj, evd, tmp_data);
}

#define chknull(v) if ((*(void **)(v)) != NULL)
#define free_switch(t, v)                                                           \
	switch (t) {                                                                \
	case DI_TYPE_ARRAY: di_free_array(*(struct di_array *)(v)); break;          \
	case DI_TYPE_STRING: chknull(v) free(*(char **)(v)); break;                 \
	case DI_TYPE_OBJECT: chknull(v) di_unref_object((v)); break;                \
	default: break;                                                             \
	}

PUBLIC void di_free_array(struct di_array arr) {
	size_t step = di_sizeof_type(arr.elem_type);
	const di_type_t et = arr.elem_type;
	for (int i = 0; i < arr.length; i++)
		free_switch(et, arr.arr + step * i);
	free(arr.arr);
}

PUBLIC void di_free_value(di_type_t t, void *ret) {
	free_switch(t, ret);
	free(ret);
}
