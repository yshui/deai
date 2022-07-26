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

PUBLIC_DEAI_API int di_call_object(di_object *nonnull o, di_type *nonnull rtype,
                                   di_value *nonnull ret, ...);
PUBLIC_DEAI_API int di_call_objectv(di_object *nonnull obj, di_type *nonnull rtype,
                                    di_value *nonnull ret, va_list);
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
