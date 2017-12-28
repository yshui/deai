/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once
#include <assert.h>
#include <ffi.h>

#include <deai/deai.h>

#include "list.h"
#include "uthash.h"

struct di_module_internal;

struct di_ev_prepare;
struct deai {
	struct di_object;
	struct ev_loop *loop;

	int argc;
	char **argv;

	char **env_copy;

	char *proctitle, *proctitle_end;

	int *exit_code;
};

struct di_module_internal {
	struct di_object;
	struct deai *di;
	UT_hash_handle hh;
};

struct di_member_internal {
	struct di_member;
	UT_hash_handle hh;
};

static_assert(sizeof(struct di_module_internal) == sizeof(struct di_module),
              "di_module size mismatch");

static ffi_type ffi_type_di_array = {
    .size = 0,
    .alignment = 0,
    .type = FFI_TYPE_STRUCT,
    .elements =
        (ffi_type *[]){&ffi_type_uint64, &ffi_type_pointer, &ffi_type_uint8, NULL},
};

static ffi_type ffi_type_di_tuple = {
    .size = 0,
    .alignment = 0,
    .type = FFI_TYPE_STRUCT,
    .elements =
        (ffi_type *[]){&ffi_type_uint64, &ffi_type_pointer, &ffi_type_pointer, NULL},
};

static_assert(sizeof(bool) == sizeof(uint8_t), "bool is not uint8_t, unsupported "
                                               "platform");
static_assert(__alignof__(bool) == __alignof__(uint8_t), "bool is not uint8_t, "
                                                         "unsupported platform");

static inline ffi_type *di_type_to_ffi(di_type_t in) {
	ffi_type *const type_map[] = {
	        [DI_TYPE_VOID] = &ffi_type_void,
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
	        [DI_LAST_TYPE] = NULL,
	};

	assert(type_map[in]);
	return type_map[in];
}

static inline ffi_status di_ffi_prep_cif(ffi_cif *cif, unsigned int nargs,
                                         di_type_t rtype, const di_type_t *atypes) {
	ffi_type *ffi_rtype = di_type_to_ffi(rtype);
	ffi_type **ffi_atypes = NULL;
	if (nargs) {
		ffi_atypes = calloc(nargs, sizeof(ffi_type *));
		for (int i = 0; i < nargs; i++)
			ffi_atypes[i] = di_type_to_ffi(atypes[i]);
	}

	ffi_status ret =
	    ffi_prep_cif(cif, FFI_DEFAULT_ABI, nargs, ffi_rtype, ffi_atypes);
	if (ret != FFI_OK)
		free(ffi_atypes);
	return ret;
}
