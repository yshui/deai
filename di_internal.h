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

#include "config.h"
#include "list.h"
#include "uthash.h"

struct di_member {
	di_string name;
	di_value *nonnull data;
	di_type type;
	UT_hash_handle hh;
};

typedef struct di_object_internal {
	struct di_member *nullable members;

	di_dtor_fn nullable dtor;
	di_call_fn nullable call;

	uint64_t ref_count;
	uint64_t weak_ref_count;
	/// A temporary ref count variable used for object tracking and reference cycle
	/// collection.
	uint64_t ref_count_scan;
	struct list_head unreferred_siblings;

#ifdef TRACK_OBJECTS
	struct list_head siblings;
	char padding[46];
#else
	// Reserved for future use
	char padding[62];
#endif
	uint8_t mark;
	uint8_t destroyed;
} di_object_internal;

#ifdef TRACK_OBJECTS
extern thread_local struct list_head all_objects;
#endif

struct di_anonymous_root {
	di_object *nonnull obj;
	UT_hash_handle hh;
};

struct di_roots {
	di_object_internal;
	struct di_anonymous_root *nullable anonymous_roots;
};

struct di_ref_tracked_object {
	void *nonnull ptr;
	UT_hash_handle hh;
};

struct deai {
	di_object_internal;
	struct ev_loop *nonnull loop;

	int argc;
	char *nullable *nullable argv;

	char *nonnull proctitle;
	char *nonnull proctitle_end;

	// An array of size proctitle_end - proctitle, copy of the origin proctitle
	// memory.
	char *nonnull orig_proctitle;

	int *nonnull exit_code;
	bool *nonnull quit;
};

struct di_module {
	di_object;
};

extern struct di_roots *nullable roots;

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
    .elements = (ffi_type *[]){&ffi_type_uint64, &ffi_type_pointer, NULL},
};

static ffi_type ffi_type_di_variant = {
    .size = 0,
    .alignment = 0,
    .type = FFI_TYPE_STRUCT,
    .elements = (ffi_type *[]){&ffi_type_pointer, &ffi_type_uint8, NULL},
};

static ffi_type ffi_type_di_string = {
    .size = 0,
    .alignment = 0,
    .type = FFI_TYPE_STRUCT,
    .elements = (ffi_type *[]){&ffi_type_pointer, &ffi_type_uint64, NULL},
};

static_assert(sizeof(bool) == sizeof(uint8_t), "bool is not uint8_t, unsupported "
                                               "platform");
static_assert(__alignof__(bool) == __alignof__(uint8_t), "bool is not uint8_t, "
                                                         "unsupported platform");

static inline ffi_type *nullable di_type_to_ffi(di_type in) {
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
	    [DI_TYPE_WEAK_OBJECT] = &ffi_type_pointer,
	    [DI_TYPE_STRING] = &ffi_type_di_string,
	    [DI_TYPE_STRING_LITERAL] = &ffi_type_pointer,
	    [DI_TYPE_ARRAY] = &ffi_type_di_array,
	    [DI_TYPE_TUPLE] = &ffi_type_di_tuple,
	    [DI_TYPE_VARIANT] = &ffi_type_di_variant,
	    [DI_LAST_TYPE] = NULL,
	};

	assert(type_map[in]);
	return type_map[in];
}

static inline ffi_status unused di_ffi_prep_cif(ffi_cif *nonnull cif, unsigned int nargs,
                                                di_type rtype, const di_type *nonnull atypes) {
	ffi_type *ffi_rtype = di_type_to_ffi(rtype);
	ffi_type **ffi_atypes = NULL;
	if (nargs) {
		ffi_atypes = calloc(nargs, sizeof(ffi_type *));
		for (int i = 0; i < nargs; i++) {
			ffi_atypes[i] = di_type_to_ffi(atypes[i]);
		}
	}

	ffi_status ret = ffi_prep_cif(cif, FFI_DEFAULT_ABI, nargs, ffi_rtype, ffi_atypes);
	if (ret != FFI_OK) {
		free(ffi_atypes);
	}
	return ret;
}

struct di_module *nullable di_new_module_with_size(di_object *nonnull di, size_t size);

di_object *nullable di_try(void (*nonnull func)(void *nullable), void *nullable args);
void di_collect_garbage(void);
#if defined(TRACK_OBJECTS) || defined(ENABLE_STACK_TRACE)
#include <elfutils/libdwfl.h>
struct stack_annotate_context;
unsigned int __attribute__((noinline))
stack_trace_get(int skip, unsigned int limit, uint64_t *nonnull ips,
                uint64_t *nonnull procs, di_string *nonnull names);
unsigned int __attribute__((noinline)) stack_trace_frame_count(void);
struct stack_annotate_context *nullable stack_trace_annotate_prepare();
di_string stack_trace_annotate(struct stack_annotate_context *nonnull ctx, uint64_t ip);
void stack_trace_annotate_end(struct stack_annotate_context *nonnull ctx);
void __attribute__((noinline)) print_stack_trace(int skip, int limit);
#endif

#ifdef TRACK_OBJECTS
void di_dump_objects(void);
void di_dump_object(di_object *nonnull obj);
void di_track_object_ref(di_object *nonnull, void *nonnull);
#else
static inline void di_dump_objects(void) {
}
static inline void di_track_object_ref(di_object *unused nonnull a, void *unused nonnull b) {
}
#endif
