/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

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

	int argc;
	char **argv;

	char *proctitle, *proctitle_end;
	bool quit;
};

struct di_method_internal {
	struct di_callable;

	char *name;
	bool typed;
	void (*free)(void *);
	UT_hash_handle hh;
};

struct di_typed_method {
	struct di_method_internal;

	struct di_object *this;
	di_fn_t real_fn_ptr;

	unsigned int nargs;
	di_type_t rtype;
	ffi_cif cif;
	di_type_t atypes[];
};

// Dynamically typed method
struct di_untyped_method {
	struct di_method_internal;

	void *user_data;
	di_callbale_t real_fn_ptr;
	free_fn_t ud_free;
};

struct di_listener {
	di_listener_fn_t f;
	void *ud;
	free_fn_t ud_free;
	struct list_head siblings;
};

struct di_module_internal {
	struct di_object;
	char *name;
	struct deai *di;
	UT_hash_handle hh;
};

struct di_error {
	struct di_object;
	char *msg;
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

static_assert(sizeof(bool) == sizeof(uint8_t), "bool is not uint8_t, unsupported "
                                               "platform");
static_assert(__alignof__(bool) == __alignof__(uint8_t), "bool is not uint8_t, "
                                                         "unsupported platform");

static inline ffi_type *di_type_to_ffi(di_type_t in) {
	ffi_type *const type_map[DI_LAST_TYPE] = {
	    &ffi_type_void,    &ffi_type_uint8,    &ffi_type_sint,
	    &ffi_type_uint,    &ffi_type_uint64,   &ffi_type_sint64,
	    &ffi_type_double,  &ffi_type_pointer,  &ffi_type_pointer,
	    &ffi_type_pointer, &ffi_type_di_array, &ffi_type_pointer};

	assert(in < DI_TYPE_NIL);
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
