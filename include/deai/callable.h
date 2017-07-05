/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once
#include <deai/object.h>

#include <stdarg.h>

struct di_closure;

int di_call_objectv(struct di_object *obj, di_type_t *rtype, void **ret, va_list);
struct di_closure *
di_create_closure(di_fn_t fn, di_type_t rtype, int ncaptures,
                  const di_type_t *captypes, const void *const *captures, int nargs,
                  const di_type_t *argtypes);
int di_add_method(struct di_object *object, const char *name, di_fn_t fn,
                  di_type_t rtype, int nargs, ...);
void di_set_this(struct di_object *fn, struct di_object *this);
