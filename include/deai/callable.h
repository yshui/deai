/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once
#include "common.h"
#include "object.h"

#include <stdarg.h>

enum {
	MAX_NARGS = 128,
};

typedef struct di_closure di_closure;

PUBLIC_DEAI_API int di_call_objectt(di_object *nonnull, di_type *nonnull,
                                    di_value *nonnull, di_tuple);
PUBLIC_DEAI_API
struct di_closure *nullable di_create_closure(void (*nonnull fn)(void), di_type rtype,
                                              di_tuple captures, int nargs,
                                              const di_type *nullable arg_types);
PUBLIC_DEAI_API int di_add_method(di_object *nonnull object, di_string name,
                                  void (*nonnull fn)(void), di_type rtype, int nargs, ...);

/// Create a field getter. This field getter can be used as a specialized getter on an
/// object, to retrieve a member of type `type` stored at `offset` inside the object
PUBLIC_DEAI_API di_object *nonnull di_new_field_getter(di_type type, ptrdiff_t offset);

#define capture(...)                                                                         \
	VA_ARGS_LENGTH(__VA_ARGS__)                                                          \
	, (di_type[]){LIST_APPLY(di_typeof, SEP_COMMA, __VA_ARGS__)}, (const di_value *[]) { \
		LIST_APPLY(addressof_di_value, SEP_COMMA, __VA_ARGS__)                       \
	}

#define capture_types(...) LIST_APPLY_pre(typeof, SEP_COMMA, __VA_ARGS__)

#define di_make_closure(fn, caps, ...)                                                        \
	di_create_closure((void *)fn, di_return_typeid(fn capture_types caps, ##__VA_ARGS__), \
	                  di_make_tuple caps, VA_ARGS_LENGTH(__VA_ARGS__),                    \
	                  (di_type[]){LIST_APPLY(di_typeid, SEP_COMMA, __VA_ARGS__)})
