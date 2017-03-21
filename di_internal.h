#pragma once
#include <ffi.h>
#include <deai.h>
#include "uthash.h"
#include "list.h"

struct di_event_desc_internal {
	const char *name;
	unsigned int nargs;
	const di_type_t *types;
	ffi_cif cif;

	UT_hash_handle hh;
};

struct di_evsrc_reg {
	struct di_event_desc_internal *evd;
};

struct di_evsrc_sub {
	char *name;
	struct list_head listeners;
	UT_hash_handle hh;
};

struct di_evsrc {
	struct di *di;
	struct di_evsrc_sub *sub;
};

struct deai {
	struct ev_loop *loop;
	struct di_module *m;
	struct di_evsrc core_ev;
};

struct di_fn_internal {
	const char *name;
	unsigned int nargs;
	di_type_t rtype;
	const di_type_t *atypes;
	void (*fn_ptr)(void);
	ffi_cif cif;
	UT_hash_handle hh;
};

struct di_listener {
	struct di_fn_internal *f;
	struct list_head siblings;
};

struct di_closure {
	const char *name;
	ffi_cif cif;
	unsigned int nargs;
	di_type_t rtype;
	const di_type_t *atypes;
	void (*fn_ptr)(void);
	UT_hash_handle hh;

	di_closure real_fn_ptr;
	void *user_data;
};

struct di_module {
	char *name;
	struct di *di;
	struct di_fn_internal *fn;
	struct di_evsrc mod_ev;
	UT_hash_handle hh;
};

extern const struct di_event_desc
	di_ev_new_module,
	di_ev_new_fn,
	di_ev_startup;

static inline ffi_type *di_type_to_ffi(di_type_t in) {
	ffi_type * const type_map[DI_LAST_TYPE] = {
		&ffi_type_void,
		&ffi_type_uint8,
		&ffi_type_uint16,
		&ffi_type_uint32,
		&ffi_type_uint64,
		&ffi_type_sint8,
		&ffi_type_sint16,
		&ffi_type_sint32,
		&ffi_type_sint64,
		&ffi_type_float,
		&ffi_type_double,
		&ffi_type_pointer,
		&ffi_type_pointer,
		&ffi_type_pointer
	};

	return type_map[in];
}

static inline ffi_status
di_ffi_prep_cif(ffi_cif *cif, unsigned int nargs,
		   di_type_t rtype, const di_type_t *atypes) {
	ffi_type *ffi_rtype = di_type_to_ffi(rtype);
	ffi_type **ffi_atypes = calloc(nargs, sizeof(void *));
	if (!ffi_atypes)
		return FFI_BAD_TYPEDEF;

	for (int i = 0; i < nargs; i++)
		ffi_atypes[i] = di_type_to_ffi(atypes[i]);

	ffi_status ret = ffi_prep_cif(cif, FFI_DEFAULT_ABI, nargs, ffi_rtype, ffi_atypes);
	if (ret != FFI_OK)
		free(ffi_atypes);
	return ret;
}
