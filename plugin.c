#include <plugin.h>

#include "di_internal.h"
#include "utils.h"

#define PUBLIC __attribute__ ((visibility("default")))

PUBLIC struct di_module *
di_module_lookup(struct deai *p, const char *mod_name) {
	struct di_module *m = NULL;
	HASH_FIND_STR(p->m, mod_name, m);
	return m;
}

PUBLIC struct di_module *
di_modules(struct deai *p) {
	return p->m;
}

PUBLIC struct di_module *
di_module_next(struct di_module *pm) {
	return pm->hh.next;
}

PUBLIC struct di_fn *
di_module_function_lookup(struct di_module *pm, const char *fn_name) {
	struct di_fn_internal *fn = NULL;
	HASH_FIND_STR(pm->fn, fn_name, fn);
	return (struct di_fn *)fn;
}

PUBLIC struct di_fn *
di_module_functions(struct di_module *pm) {
	return (struct di_fn *)pm->fn;
}

PUBLIC struct di_fn *
di_module_function_next(struct di_fn *fn) {
	return (struct di_fn *)((struct di_fn_internal *)fn)->hh.next;
}

PUBLIC struct di_module *
di_module_new(const char *name) {
	struct di_module *pm = NULL;
	pm = tmalloc(struct di_module, 1);
	pm->name = strdup(name);
	return pm;
}

PUBLIC int
di_register_module(struct deai *p, struct di_module *pm) {
	struct di_module *old_pm = NULL;
	HASH_FIND_STR(p->m, pm->name, old_pm);
	if (old_pm)
		return -EEXIST;
	HASH_ADD_KEYPTR(hh, p->m, pm->name, strlen(pm->name), pm);

	di_type_t atype = DI_TYPE_STRING;
	void *args[1] = { &pm->name };
	di_event_source_emit(&p->core_ev, &di_ev_new_module, args);
	return 0;
}

PUBLIC struct di_fn *
di_callable_create_fn(void (*fn)(void), unsigned int nargs,
			 di_type_t rtype, const di_type_t *atypes,
			 const char *name) {
	struct di_fn_internal *f = tmalloc(struct di_fn_internal, 1);
	if (!f)
		return NULL;

	f->rtype = rtype;
	f->atypes = atypes;
	f->fn_ptr = fn;
	f->name = name;

	ffi_status ret =
	    di_ffi_prep_cif(&f->cif, nargs, f->rtype, f->atypes);

	if (ret != FFI_OK) {
		free(f);
		return NULL;
	}

	return (void *)f;
}

void _di_closure_trampoline(ffi_cif *cif, void *ret, void **args, void *user_data) {
	struct di_closure *cl = user_data;
	cl->real_fn_ptr(user_data, ret, args, cl->user_data);
}

PUBLIC struct di_fn *
di_callable_create_closure(di_closure cl, unsigned int nargs,
			      di_type_t rtype, const di_type_t *atypes,
			      void *user_data, const char *name) {
	struct di_closure *c = tmalloc(struct di_closure, 1);
	if (!c)
		return NULL;

	c->rtype = rtype;
	c->atypes = atypes;
	c->real_fn_ptr = cl;
	c->name = name;
	c->user_data = user_data;

	ffi_status ret =
	    di_ffi_prep_cif(&c->cif, nargs, c->rtype, c->atypes);

	if (ret != FFI_OK)
		goto err_ret;

	void *writable, *code;
	writable = ffi_closure_alloc(sizeof(ffi_closure), &code);
	if (!writable)
		goto err_ret;

	ret = ffi_prep_closure_loc(writable, &c->cif, &_di_closure_trampoline,
				   (void *)c, code);

	if (ret != FFI_OK)
		goto err_ret2;

	c->fn_ptr = code;
	return (void *)c;

err_ret2:
	ffi_closure_free(writable);
err_ret:
	free(c);
	return NULL;
}

PUBLIC int
di_register_fn(struct di_module *pm, struct di_fn *_f) {
	struct di_fn_internal *f = (void *)_f, *old_f;
	if (!f->name)
		return -EINVAL; //Can't register unnamed function

	HASH_FIND_STR(pm->fn, f->name, old_f);
	if (old_f)
		return -EEXIST;

	HASH_ADD_KEYPTR(hh, pm->fn, f->name, strlen(f->name), f);

	di_type_t atype = DI_TYPE_STRING;
	void *args[1] = { &f->name };
	di_event_source_emit(&pm->mod_ev, &di_ev_new_fn, args);
	return 0;
}

PUBLIC struct di_evsrc_reg *
di_event_source_registry_new(void) {
	return tmalloc(struct di_evsrc_reg, 1);
}

PUBLIC int
di_event_source_registry_add_event(struct di_evsrc_reg *r, const struct di_event_desc *_evd) {
	struct di_event_desc_internal *evd = NULL;
	HASH_FIND_STR(r->evd, _evd->name, evd);
	if (evd)
		return -EEXIST;

	evd = tmalloc(struct di_event_desc_internal, 1);
	if (!evd)
		return -ENOMEM;

	memcpy(evd, _evd, sizeof(*_evd));

	ffi_status ret =
	    di_ffi_prep_cif(&evd->cif, evd->nargs, DI_TYPE_VOID, evd->types);

	if (ret != FFI_OK) {
		free(evd);
		return -EINVAL;
	}

	HASH_ADD_KEYPTR(hh, r->evd, evd->name, strlen(evd->name), evd);
	return 0;
}

PUBLIC struct di_event_desc *
di_event_source_registry_lookup(struct di_evsrc_reg *r, const char *ev_name) {
	struct di_event_desc_internal *evd = NULL;
	HASH_FIND_STR(r->evd, ev_name, evd);
	return (void *)evd;
}
