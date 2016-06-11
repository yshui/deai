#pragma once
#include <ffi.h>
#include <piped.h>
#include "uthash.h"
#include "list.h"

struct piped_event_desc_internal {
	const char *name;
	unsigned int nargs;
	const piped_type_t *types;
	ffi_cif cif;

	UT_hash_handle hh;
};

struct piped_evsrc_reg {
	struct piped_event_desc_internal *evd;
};

struct piped_evsrc_sub {
	char *name;
	struct list_head listeners;
	UT_hash_handle hh;
};

struct piped_evsrc {
	struct piped *piped;
	struct piped_evsrc_sub *sub;
};

struct piped {
	struct ev_loop *loop;
	struct piped_module *m;
	struct piped_evsrc core_ev;
};

struct piped_fn_internal {
	const char *name;
	ffi_cif cif;
	unsigned int nargs;
	piped_type_t rtype;
	const piped_type_t *atypes;
	void (*fn_ptr)(void);
	UT_hash_handle hh;
};

struct piped_listener {
	struct piped_fn_internal *f;
	struct list_head siblings;
};

struct piped_closure {
	const char *name;
	ffi_cif cif;
	unsigned int nargs;
	piped_type_t rtype;
	const piped_type_t *atypes;
	void (*fn_ptr)(void);
	UT_hash_handle hh;

	piped_closure real_fn_ptr;
	void *user_data;
};

struct piped_module {
	char *name;
	struct piped *piped;
	struct piped_fn_internal *fn;
	struct piped_evsrc mod_ev;
	UT_hash_handle hh;
};

extern const struct piped_event_desc
	piped_ev_new_module,
	piped_ev_new_fn,
	piped_ev_startup;
