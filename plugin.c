#include <assert.h>
#include <plugin.h>
#include <stdarg.h>

#include "di_internal.h"
#include "utils.h"

#define PUBLIC __attribute__((visibility("default")))

struct di_object_internal {
	struct di_method_internal *fn;
	struct di_signal *sd;
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
	di_unref_object((void *)m);
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

PUBLIC void di_init_object(struct di_object *obj) {
	obj->evd = NULL;
	obj->fn = NULL;
	obj->ref_count = 1;
	di_register_signal((void *)obj, "new-method", 1, DI_TYPE_STRING);
}

static void di_free_module(struct di_module *_m) {
	struct di_module_internal *m = (void *)_m;
	free(m->name);
}

PUBLIC struct di_module *di_new_module(const char *name, size_t size) {
	if (size < sizeof(struct di_module))
		return NULL;

	struct di_module_internal *pm = calloc(1, size);
	di_init_object((void *)pm);

	pm->name = strdup(name);

	struct di_typed_method *dtor = di_create_typed_method(
	    (di_fn_t)di_free_module, "__dtor", DI_TYPE_VOID, 1, DI_TYPE_OBJECT);
	di_register_typed_method((void *)pm, dtor);

	di_ref_object((void *)pm);

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
			di_va_arg_with_di_type(ap, t, args[i]);
		}
		va_end(ap);
	}

	return di_call_callable(c, rtype, ret, nargs, ats, (const void *const *)args);
}

static void di_free_signal(struct di_signal *sig) {
	struct di_listener *l, *ln;
	list_for_each_entry_safe(l, ln, &sig->listeners, siblings) {
		list_del(&l->siblings);
		free(l);
	}
	free(sig->name);
	free(sig->cif.arg_types);
	free(sig);
}

PUBLIC void di_free_object(struct di_object *_obj) {
	struct di_object_internal *obj = (void *)_obj;
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

	auto sig = obj->sd;
	while (sig) {
		auto next_sig = sig->hh.next;
		HASH_DEL(obj->sd, sig);
		di_free_signal(sig);
		sig = next_sig;
	}
	free(obj);
}

PUBLIC void di_ref_object(struct di_object *obj) {
	obj->ref_count++;
}

PUBLIC void di_unref_object(struct di_object *obj) {
	assert(obj->ref_count > 0);
	obj->ref_count--;
	if (obj->ref_count == 0)
		di_free_object(obj);
}

PUBLIC int di_register_module(struct deai *p, struct di_module *_m) {
	struct di_module_internal *m = (void *)_m;
	struct di_module_internal *old_m = NULL;
	HASH_FIND_STR(p->m, m->name, old_m);
	if (old_m)
		return -EEXIST;
	HASH_ADD_KEYPTR(hh, p->m, m->name, strlen(m->name), m);
	di_ref_object((void *)m);

	di_emit_signal_v((void *)p, "new-module", m->name);
	return 0;
}

static int
di_typed_trampoline(di_type_t *rt, void **ret, unsigned int nargs,
                    const di_type_t *ats, const void *const *args, void *ud) {
	struct di_typed_method *fn = ud;

	if (nargs != fn->nargs)
		return -EINVAL;

	assert(nargs == 0 || args != NULL);

	// TODO type check
	(void)ats;

	void **xargs = alloca(sizeof(void *) * (nargs + 1));
	if (args)
		memcpy(xargs + 1, args, nargs * sizeof(void *));
	xargs[0] = alloca(sizeof(void *));
	*(void **)xargs[0] = fn->this;

	if (fn->rtype != DI_TYPE_VOID)
		*ret = malloc(sizeof(void *));
	else
		*ret = NULL;

	ffi_call(&fn->cif, fn->real_fn_ptr, *ret, xargs);

	*rt = fn->rtype;

	return 0;
}

static int
di_untyped_trampoline(di_type_t *rt, void **ret, unsigned int nargs,
                      const di_type_t *ats, const void *const *args, void *ud) {
	struct di_untyped_method *gfn = ud;
	return gfn->real_fn_ptr(rt, ret, nargs, ats, args, gfn->user_data);
}

static void di_free_typed_method(void *m) {
	struct di_typed_method *tm = m;
	free(tm->cif.arg_types);
	free(tm);
}

PUBLIC struct di_typed_method *
di_create_typed_method(void (*fn)(void), const char *name, di_type_t rtype,
                       unsigned int nargs, ...) {
	auto f = (struct di_typed_method *)calloc(
	    1, sizeof(struct di_typed_method) + sizeof(void *) * (1 + nargs));
	if (!f)
		return NULL;

	f->rtype = rtype;
	f->fn_ptr = di_typed_trampoline;
	f->real_fn_ptr = fn;
	f->name = name;

	f->free = di_free_typed_method;

	va_list ap;
	va_start(ap, nargs);
	for (unsigned int i = 0; i < nargs; i++)
		f->atypes[i + 1] = va_arg(ap, di_type_t);
	va_end(ap);

	f->atypes[0] = DI_TYPE_OBJECT;
	f->nargs = nargs;

	ffi_status ret = di_ffi_prep_cif(&f->cif, nargs + 1, f->rtype, f->atypes);

	if (ret != FFI_OK) {
		free(f);
		return NULL;
	}

	return (void *)f;
}

PUBLIC struct di_untyped_method *
di_create_untyped_method(di_callbale_t fn, const char *name, void *user_data) {
	auto gfn = tmalloc(struct di_untyped_method, 1);
	gfn->user_data = user_data;
	gfn->real_fn_ptr = fn;
	gfn->fn_ptr = di_untyped_trampoline;
	gfn->name = name;
	gfn->free = NULL;

	return gfn;
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

	di_emit_signal_v(_obj, "new-method", f->name);
	return 0;
}

PUBLIC int di_register_typed_method(struct di_object *obj, struct di_typed_method *f) {
	int ret = di_register_method(obj, (void *)f);
	if (ret == 0)
		f->this = obj;
	return ret;
}

static inline void free_evd(struct di_signal **evd) {
	if (*evd) {
		if ((*evd)->name)
			free((*evd)->name);
		free(*evd);
	}
}

PUBLIC int di_register_signal(struct di_object *r, const char *name, int nargs, ...) {
	cleanup(free_evd) struct di_signal *evd = NULL;
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
	for (int i = 0; i < nargs; i++)
		evd->types[i + 1] = va_arg(ap, di_type_t);
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

static void di_typed_listener_trampoline(struct di_signal *sig, void **ev_data) {
	struct di_listener_data *ld = *(struct di_listener_data **)ev_data[0];
	struct di_listener *l = ld->user_data;
	ld->user_data = l->ud;
	ffi_call(&sig->cif, l->typed_f, NULL, ev_data);
	ld->user_data = l;
}

PUBLIC const di_type_t *
di_get_signal_arg_types(struct di_signal *sig, unsigned int *nargs) {
	*nargs = sig->nargs;
	return &sig->types[1];
}

PUBLIC struct di_listener *
di_add_untyped_listener(struct di_object *obj, const char *name, void *ud,
                        void (*f)(struct di_signal *, void **)) {
	struct di_signal *evd = NULL;
	HASH_FIND_STR(obj->evd, name, evd);
	if (!evd)
		return ERR_PTR(-ENOENT);

	struct di_listener *l = tmalloc(struct di_listener, 1);
	if (!l)
		return ERR_PTR(-ENOMEM);

	l->f = f;
	l->ud = ud;
	list_add(&l->siblings, &evd->listeners);
	return l;
}

PUBLIC struct di_listener *
di_add_typed_listener(struct di_object *obj, const char *name, void *ud, di_fn_t f) {
	auto l =
	    di_add_untyped_listener(obj, name, NULL, di_typed_listener_trampoline);
	if (IS_ERR(l))
		return l;

	l->ud = l;
	l->typed_f = f;
	return l;
}

PUBLIC void *
di_remove_listener(struct di_object *o, const char *name, struct di_listener *l) {
	struct di_signal *evd = NULL;

	HASH_FIND_STR(o->evd, name, evd);
	if (!evd)
		return ERR_PTR(-ENOENT);

	struct di_listener *p = NULL;
	list_for_each_entry(p, &evd->listeners, siblings) if (p == l) goto del;
	return ERR_PTR(-ENOENT);

del:
	list_del(&l->siblings);

	void *tmp = l->ud;
	free(l);
	return tmp;
}

static int
_di_emit_signal(struct di_object *obj, struct di_signal *evd, void **ev_data) {
	struct di_listener *l, *nl;
	struct di_listener_data ld;
	void *arg0 = &ld;
	ev_data[0] = &arg0;
	ld.obj = obj;

	list_for_each_entry_safe(l, nl, &evd->listeners, siblings) {
		// Allow remove listener from listener
		ld.user_data = l->ud;
		l->f(evd, ev_data);
	}
	return 0;
}

PUBLIC int di_emit_signal(struct di_object *obj, const char *name, void **data) {
	struct di_signal *evd = NULL;
	HASH_FIND_STR(obj->evd, name, evd);
	if (!evd)
		return -ENOENT;

	void **tmp_data = alloca(sizeof(void *) * evd->nargs + 1);
	memcpy(tmp_data + 1, data, sizeof(void *) * evd->nargs);
	return _di_emit_signal(obj, evd, tmp_data);
}

PUBLIC int di_emit_signal_v(struct di_object *obj, const char *name, ...) {
	struct di_signal *evd = NULL;
	va_list ap;

	HASH_FIND_STR(obj->evd, name, evd);
	if (!evd)
		return -ENOENT;

	void **tmp_data = alloca(sizeof(void *) * evd->nargs + 1);
	va_start(ap, name);
	for (unsigned int i = 0; i < evd->nargs; i++) {
		assert(di_sizeof_type(evd->types[i + 1]) != 0);
		tmp_data[i + 1] = alloca(di_sizeof_type(evd->types[i + 1]));
		di_va_arg_with_di_type(ap, evd->types[i + 1], tmp_data[i + 1]);
	}
	va_end(ap);

	return _di_emit_signal(obj, evd, tmp_data);
}
