#include <plugin.h>

#include "piped_internal.h"
#include "utils.h"

#define PUBLIC __attribute__ ((visibility("default")))

PUBLIC struct piped_module *
piped_module_lookup(struct piped *p, const char *mod_name) {
	struct piped_module *m = NULL;
	HASH_FIND_STR(p->m, mod_name, m);
	return m;
}

PUBLIC struct piped_module *
piped_modules(struct piped *p) {
	return p->m;
}

PUBLIC struct piped_module *
piped_module_next(struct piped_module *pm) {
	return pm->hh.next;
}

PUBLIC struct piped_fn *
piped_module_function_lookup(struct piped_module *pm, const char *fn_name) {
	struct piped_fn_internal *fn = NULL;
	HASH_FIND_STR(pm->fn, fn_name, fn);
	return (struct piped_fn *)fn;
}

PUBLIC struct piped_fn *
piped_module_functions(struct piped_module *pm) {
	return (struct piped_fn *)pm->fn;
}

PUBLIC struct piped_fn *
piped_module_function_next(struct piped_fn *fn) {
	return (struct piped_fn *)((struct piped_fn_internal *)fn)->hh.next;
}

PUBLIC struct piped_module *
piped_module_new(const char *name) {
	struct piped_module *pm = NULL;
	pm = tmalloc(struct piped_module, 1);
	pm->name = strdup(name);
	return pm;
}

PUBLIC int
piped_register_module(struct piped *p, struct piped_module *pm) {
	struct piped_module *old_pm = NULL;
	HASH_FIND_STR(p->m, pm->name, old_pm);
	if (old_pm)
		return -EEXIST;
	HASH_ADD_KEYPTR(hh, p->m, pm->name, strlen(pm->name), pm);

	piped_type_t atype = PIPED_TYPE_STRING;
	void *args[1] = { &pm->name };
	piped_event_source_emit(&p->core_ev, &piped_ev_new_module, args);
	return 0;
}

PUBLIC struct piped_fn *
piped_callable_create_fn(void (*fn)(void), unsigned int nargs,
			 piped_type_t rtype, const piped_type_t *atypes,
			 const char *name) {
	struct piped_fn_internal *f = tmalloc(struct piped_fn_internal, 1);
	if (!f)
		return NULL;

	f->rtype = rtype;
	f->atypes = atypes;
	f->fn_ptr = fn;
	f->name = name;

	ffi_status ret =
	    piped_ffi_prep_cif(&f->cif, nargs, f->rtype, f->atypes);

	if (ret != FFI_OK) {
		free(f);
		return NULL;
	}

	return (void *)f;
}

void _piped_closure_trampoline(ffi_cif *cif, void *ret, void **args, void *user_data) {
	struct piped_closure *cl = user_data;
	cl->real_fn_ptr(user_data, ret, args, cl->user_data);
}

PUBLIC struct piped_fn *
piped_callable_create_closure(piped_closure cl, unsigned int nargs,
			      piped_type_t rtype, const piped_type_t *atypes,
			      void *user_data, const char *name) {
	struct piped_closure *c = tmalloc(struct piped_closure, 1);
	if (!c)
		return NULL;

	c->rtype = rtype;
	c->atypes = atypes;
	c->real_fn_ptr = cl;
	c->name = name;
	c->user_data = user_data;

	ffi_status ret =
	    piped_ffi_prep_cif(&c->cif, nargs, c->rtype, c->atypes);

	if (ret != FFI_OK)
		goto err_ret;

	void *writable, *code;
	writable = ffi_closure_alloc(sizeof(ffi_closure), &code);
	if (!writable)
		goto err_ret;

	ret = ffi_prep_closure_loc(writable, &c->cif, &_piped_closure_trampoline,
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
piped_register_fn(struct piped_module *pm, struct piped_fn *_f) {
	struct piped_fn_internal *f = (void *)_f, *old_f;
	if (!f->name)
		return -EINVAL; //Can't register unnamed function

	HASH_FIND_STR(pm->fn, f->name, old_f);
	if (old_f)
		return -EEXIST;

	HASH_ADD_KEYPTR(hh, pm->fn, f->name, strlen(f->name), f);

	piped_type_t atype = PIPED_TYPE_STRING;
	void *args[1] = { &f->name };
	piped_event_source_emit(&pm->mod_ev, &piped_ev_new_fn, args);
	return 0;
}

PUBLIC struct piped_evsrc_reg *
piped_event_source_registry_new(void) {
	return tmalloc(struct piped_evsrc_reg, 1);
}

PUBLIC int
piped_event_source_registry_add_event(struct piped_evsrc_reg *r, const struct piped_event_desc *_evd) {
	struct piped_event_desc_internal *evd = NULL;
	HASH_FIND_STR(r->evd, _evd->name, evd);
	if (evd)
		return -EEXIST;

	evd = tmalloc(struct piped_event_desc_internal, 1);
	if (!evd)
		return -ENOMEM;

	memcpy(evd, _evd, sizeof(*_evd));

	ffi_status ret =
	    piped_ffi_prep_cif(&evd->cif, evd->nargs, PIPED_TYPE_VOID, evd->types);

	if (ret != FFI_OK) {
		free(evd);
		return -EINVAL;
	}

	HASH_ADD_KEYPTR(hh, r->evd, evd->name, strlen(evd->name), evd);
	return 0;
}

PUBLIC struct piped_event_desc *
piped_event_source_registry_lookup(struct piped_evsrc_reg *r, const char *ev_name) {
	struct piped_event_desc_internal *evd = NULL;
	HASH_FIND_STR(r->evd, ev_name, evd);
	return (void *)evd;
}
