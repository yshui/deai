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

int di_call_object(struct di_object *nonnull o, di_type_t *nonnull rtype,
                   void *nullable *nonnull ret, ...);
int di_call_objectv(struct di_object *nonnull obj, di_type_t *nonnull rtype,
                    void *nullable *nonnull ret, va_list);
int di_call_objectt(struct di_object *nonnull, di_type_t *nonnull,
                    void *nullable *nonnull, struct di_tuple);
struct di_closure *nullable di_create_closure(void (*nonnull fn)(void), di_type_t rtype,
                                              int ncaptures, const di_type_t *nullable captypes,
                                              const void *nonnull const *nullable captures,
                                              int nargs, const di_type_t *nullable argtypes,
                                              bool weak_capture);
int di_add_method(struct di_object *nonnull object, const char *nonnull name,
                  void (*nonnull fn)(void), di_type_t rtype, int nargs, ...);
