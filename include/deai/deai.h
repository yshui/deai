/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, 2020 Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "callable.h"
#include "common.h"
#include "compiler.h"
#include "object.h"
#include "error.h"

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

PUBLIC_DEAI_API struct di_module *di_new_module(struct deai *);
PUBLIC_DEAI_API int di_register_module(struct deai *, di_string, struct di_module **);

#define di_new_object_with_type(type) (type *)di_new_object(sizeof(type), alignof(type))
#define di_new_object_with_type2(type, di_type)                                          \
	(type *)di_new_object_with_type_name(sizeof(type), alignof(type), di_type)

/// Define a entry point for a deai plugin. Your entry point function will take a single
/// argument `arg`, which points to the core deai object.
#define DEAI_PLUGIN_ENTRY_POINT(arg)                                                     \
	visibility_default int di_plugin_init(struct deai *arg)
