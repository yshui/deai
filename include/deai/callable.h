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

int di_call_objectv(struct di_object *_Nonnull obj, di_type_t *_Nonnull rtype,
                    void *_Nullable *_Nonnull ret, va_list);
struct di_closure *_Nullable di_create_closure(
    void (*_Nonnull fn)(void), di_type_t rtype, int ncaptures,
    const di_type_t *_Nullable captypes,
    const void *_Nonnull const *_Nullable captures, int nargs,
    const di_type_t *_Nullable argtypes, bool weak_capture);
int di_add_method(struct di_object *_Nonnull object, const char *_Nonnull name,
                  void (*_Nonnull fn)(void), di_type_t rtype, int nargs, ...);
