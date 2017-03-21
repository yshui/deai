#pragma once
#include "list.h"
#include "uthash.h"
#include <assert.h>
#include <deai.h>
#include <ffi.h>

struct di_module_internal;

struct di_signal {
	char *name;
	unsigned int nargs;
	ffi_cif cif;

	UT_hash_handle hh;
	struct list_head listeners;
	di_type_t types[];
};

struct deai {
	struct di_object;
	struct ev_loop *loop;
	struct di_module_internal *m;
};

struct di_method_internal {
	struct di_callable;

	const char *name;
	UT_hash_handle hh;
};

struct di_typed_method {
	struct di_method_internal;

	struct di_object *this;
	unsigned int nargs;
	di_type_t rtype;
	ffi_cif cif;
	di_fn_t real_fn_ptr;
	di_type_t atypes[];
};

// Dynamically typed method
struct di_untyped_method {
	struct di_method_internal;

	void *user_data;
	di_callbale_t real_fn_ptr;
};

struct di_listener {
	di_fn_t f;
	void *ud;
	struct list_head siblings;
};

struct di_module_internal {
	struct di_object;
	char *name;
	struct deai *di;
	UT_hash_handle hh;
};

static_assert(sizeof(struct di_module_internal) == sizeof(struct di_module),
              "di_module size mismatch");

static inline ffi_type *di_type_to_ffi(di_type_t in) {
	ffi_type *const type_map[DI_LAST_TYPE] = {
	    &ffi_type_void,    &ffi_type_uint8,  &ffi_type_uint16, &ffi_type_uint32,
	    &ffi_type_uint64,  &ffi_type_sint8,  &ffi_type_sint16, &ffi_type_sint32,
	    &ffi_type_sint64,  &ffi_type_float,  &ffi_type_double, &ffi_type_pointer,
	    &ffi_type_pointer, &ffi_type_pointer};

	return type_map[in];
}

static inline ffi_status di_ffi_prep_cif(ffi_cif *cif, unsigned int nargs,
                                         di_type_t rtype, const di_type_t *atypes) {
	ffi_type *ffi_rtype = di_type_to_ffi(rtype);
	ffi_type **ffi_atypes = NULL;
	if (nargs) {
		ffi_atypes = calloc(nargs, sizeof(void *));
		if (!ffi_atypes)
			return FFI_BAD_TYPEDEF;

		for (int i = 0; i < nargs; i++)
			ffi_atypes[i] = di_type_to_ffi(atypes[i]);
	}

	ffi_status ret =
	    ffi_prep_cif(cif, FFI_DEFAULT_ABI, nargs, ffi_rtype, ffi_atypes);
	if (ret != FFI_OK)
		free(ffi_atypes);
	return ret;
}
