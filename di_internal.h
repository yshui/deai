/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once
#include <assert.h>
#include <ffi.h>
#include <stdalign.h>
#include <threads.h>

#include <deai/deai.h>

#include "list.h"
#include "uthash.h"

struct di_ev_prepare;

struct di_member {
	char *nonnull name;
	void *nonnull data;
	di_type_t type;
	bool own;
	UT_hash_handle hh;
};

struct di_object_internal {
	struct di_member *nullable members;
	struct di_signal *nullable signals;

	di_dtor_fn_t nullable dtor;
	di_call_fn_t nullable call;

	uint64_t ref_count;
	uint8_t destroyed;

#ifdef TRACK_OBJECTS
	char padding[7];
	struct list_head siblings;
#else
	// Reserved for future use
	char padding[23];
#endif
};

#ifdef TRACK_OBJECTS
extern thread_local struct list_head all_objects;
#endif

struct deai {
	struct di_object_internal;
	struct ev_loop *nonnull loop;

	int argc;
	char *nullable *nonnull argv;

	char *nonnull proctitle;
	char *nonnull proctitle_end;

	int *nonnull exit_code;
	bool *nonnull quit;
};

struct di_module {
	struct di_object;
	struct deai *nonnull di;
	UT_hash_handle hh;
};

static ffi_type ffi_type_di_array = {
    .size = 0,
    .alignment = 0,
    .type = FFI_TYPE_STRUCT,
    .elements = (ffi_type *[]){&ffi_type_uint64, &ffi_type_pointer, &ffi_type_uint8, NULL},
};

static ffi_type ffi_type_di_tuple = {
    .size = 0,
    .alignment = 0,
    .type = FFI_TYPE_STRUCT,
    .elements = (ffi_type *[]){&ffi_type_uint64, &ffi_type_pointer, &ffi_type_pointer, NULL},
};

static ffi_type ffi_type_di_variant = {
    .size = 0,
    .alignment = 0,
    .type = FFI_TYPE_STRUCT,
    .elements = (ffi_type *[]){&ffi_type_pointer, &ffi_type_uint8, NULL},
};

static_assert(sizeof(bool) == sizeof(uint8_t), "bool is not uint8_t, unsupported "
                                               "platform");
static_assert(__alignof__(bool) == __alignof__(uint8_t), "bool is not uint8_t, "
                                                         "unsupported platform");

static inline ffi_type *nullable di_type_to_ffi(di_type_t in) {
	ffi_type *const type_map[] = {
	    [DI_TYPE_NIL] = &ffi_type_void,
	    [DI_TYPE_BOOL] = &ffi_type_uint8,
	    [DI_TYPE_NINT] = &ffi_type_sint,
	    [DI_TYPE_NUINT] = &ffi_type_uint,
	    [DI_TYPE_UINT] = &ffi_type_uint64,
	    [DI_TYPE_INT] = &ffi_type_sint64,
	    [DI_TYPE_FLOAT] = &ffi_type_double,
	    [DI_TYPE_POINTER] = &ffi_type_pointer,
	    [DI_TYPE_OBJECT] = &ffi_type_pointer,
	    [DI_TYPE_STRING] = &ffi_type_pointer,
	    [DI_TYPE_STRING_LITERAL] = &ffi_type_pointer,
	    [DI_TYPE_ARRAY] = &ffi_type_di_array,
	    [DI_TYPE_TUPLE] = &ffi_type_di_tuple,
	    [DI_TYPE_VARIANT] = &ffi_type_di_variant,
	    [DI_LAST_TYPE] = NULL,
	};

	assert(type_map[in]);
	return type_map[in];
}

static inline ffi_status di_ffi_prep_cif(ffi_cif *nonnull cif, unsigned int nargs,
                                         di_type_t rtype, const di_type_t *nonnull atypes) {
	ffi_type *ffi_rtype = di_type_to_ffi(rtype);
	ffi_type **ffi_atypes = NULL;
	if (nargs) {
		ffi_atypes = calloc(nargs, sizeof(ffi_type *));
		for (int i = 0; i < nargs; i++)
			ffi_atypes[i] = di_type_to_ffi(atypes[i]);
	}

	ffi_status ret = ffi_prep_cif(cif, FFI_DEFAULT_ABI, nargs, ffi_rtype, ffi_atypes);
	if (ret != FFI_OK)
		free(ffi_atypes);
	return ret;
}

struct di_module *nullable di_new_module_with_size(struct deai *nonnull di, size_t size);

#ifdef TRACK_OBJECTS
void di_dump_objects(void);
#endif
