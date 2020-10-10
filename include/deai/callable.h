/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once
#include <deai/object.h>

#include <stdarg.h>

enum { MAX_NARGS = 128,
};

struct di_closure;

PUBLIC int di_call_object(struct di_object *nonnull o, di_type_t *nonnull rtype,
                          union di_value *nonnull ret, ...);
PUBLIC int di_call_objectv(struct di_object *nonnull obj, di_type_t *nonnull rtype,
                           union di_value *nonnull ret, va_list);
PUBLIC int di_call_objectt(struct di_object *nonnull, di_type_t *nonnull,
                           union di_value *nonnull, struct di_tuple);
PUBLIC
struct di_closure *nullable di_create_closure(void (*nonnull fn)(void), di_type_t rtype,
                                              int ncaptures,
                                              const di_type_t *nullable capture_types,
                                              const union di_value *nullable const *nullable captures,
                                              int nargs, const di_type_t *nullable arg_types);
PUBLIC int di_add_method(struct di_object *nonnull object, const char *nonnull name,
                         void (*nonnull fn)(void), di_type_t rtype, int nargs, ...);

/// Create a field getter. This field getter can be used as a specialized getter on an
/// object, to retrieve a member of type `type` stored at `offset` inside the object
PUBLIC struct di_object *nonnull di_new_field_getter(di_type_t type, ptrdiff_t offset);
