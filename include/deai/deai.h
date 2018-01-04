/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <deai/compiler.h>
#include <deai/object.h>
#include <deai/callable.h>

struct deai;

#define MAX_ERRNO 4095

static inline void *ERR_PTR(long err) {
	return (void *)err;
}

static inline long PTR_ERR(const void *ptr) {
	return (long)ptr;
}

#define unlikely(x) (__builtin_constant_p(x) ? !!(x) : __builtin_expect(!!(x), 0))

#define IS_ERR_VALUE(x) unlikely((x) >= (unsigned long)-MAX_ERRNO)

static inline bool IS_ERR(const void *ptr) {
	return IS_ERR_VALUE((unsigned long)ptr);
}

static inline bool IS_ERR_OR_NULL(const void *ptr) {
	return unlikely(!ptr) || IS_ERR_VALUE((unsigned long)ptr);
}

typedef void (*init_fn_t)(struct deai *);

struct di_module *di_new_module(size_t);
int di_register_module(struct deai *, const char *, struct di_module **);

#define di_new_module_with_type(type) (struct di_module *)di_new_module(sizeof(type))
#define di_new_object_with_type(type) (type *)di_new_object(sizeof(type))
