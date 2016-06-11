#pragma once
#include <ffi.h>
#include <stdbool.h>
#include <stdlib.h>

struct piped;
struct piped_module;
struct piped_evsrc;
struct piped_evsrc_reg;

typedef enum piped_type {
	PIPED_TYPE_VOID = 0,
	PIPED_TYPE_UINT8,
	PIPED_TYPE_UINT16,
	PIPED_TYPE_UINT32,
	PIPED_TYPE_UINT64,
	PIPED_TYPE_INT8,
	PIPED_TYPE_INT16,
	PIPED_TYPE_INT32,
	PIPED_TYPE_INT64,
	PIPED_TYPE_FLOAT,
	PIPED_TYPE_DOUBLE,
	PIPED_TYPE_POINTER,       //Generic pointer
	PIPED_TYPE_EVENT_SOURCE,  //struct piped_evsrc*
	PIPED_TYPE_STRING,        //char*
	PIPED_LAST_TYPE
} piped_type_t;

struct piped_fn {
	const char *name;
	const ffi_cif cif;
	const void (*fn_ptr)(void);
};

struct piped_event_desc {
	const char *name;
	unsigned int nargs;
	const piped_type_t *types;
};

#define MAX_ERRNO 4095

static inline void * ERR_PTR(long err) {
	return (void *)err;
}

static inline long PTR_ERR(const void *ptr) {
	return (long)ptr;
}

static inline ffi_type *piped_type_to_ffi(piped_type_t in) {
	ffi_type * const type_map[PIPED_LAST_TYPE] = {
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
piped_ffi_prep_cif(ffi_cif *cif, unsigned int nargs,
		   piped_type_t rtype, const piped_type_t *atypes) {
	ffi_type *ffi_rtype = piped_type_to_ffi(rtype);
	ffi_type **ffi_atypes = calloc(nargs, sizeof(void *));
	if (!ffi_atypes)
		return FFI_BAD_TYPEDEF;

	for (int i = 0; i < nargs; i++)
		ffi_atypes[i] = piped_type_to_ffi(atypes[i]);

	ffi_status ret = ffi_prep_cif(cif, FFI_DEFAULT_ABI, nargs, ffi_rtype, ffi_atypes);
	if (ret != FFI_OK)
		free(ffi_atypes);
	return ret;
}

#define unlikely(x) (__builtin_constant_p(x) ? !!(x) : __builtin_expect(!!(x), 0))

#define IS_ERR_VALUE(x) unlikely((x) >= (unsigned long)-MAX_ERRNO)

static inline bool IS_ERR(const void *ptr) {
	return IS_ERR_VALUE((unsigned long)ptr);
}

static inline bool IS_ERR_OR_NULL(const void *ptr) {
	return unlikely(!ptr) || IS_ERR_VALUE((unsigned long)ptr);
}

typedef int (*piped_closure)(struct piped_fn *fn, void *ret, void **args, void *user_data);

struct piped_evsrc *piped_event_source_new(void);
int piped_event_source_add_listener(struct piped_evsrc *, const char *ev_name, struct piped_fn *f);
int piped_event_source_emit(struct piped_evsrc *, const struct piped_event_desc *, void **ev_data);

struct piped_evsrc *piped_core_event_source(struct piped *);
struct piped_evsrc *piped_module_event_source(struct piped_module *);

int
piped_event_source_registry_add_event(struct piped_evsrc_reg *, const struct piped_event_desc *);
