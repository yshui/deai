/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

#include <stdbool.h>
#include <string.h>

#include <deai/compiler.h>
#include <deai/helper.h>
#include <deai/object.h>
#include "common.h"

/// Fetch a value based on di_type from va_arg, and put it into `buf` if `buf` is
/// not NULL. This function only borrows the value, without cloning it.
static inline void unused va_arg_with_di_type(va_list ap, di_type t, void *buf) {
	di_value v;

	switch (t) {
	case DI_TYPE_STRING_LITERAL:
	case DI_TYPE_POINTER:
	case DI_TYPE_OBJECT:
	case DI_TYPE_EMPTY_OBJECT:
	case DI_TYPE_WEAK_OBJECT:
		v.pointer = va_arg(ap, void *);
		break;
	case DI_TYPE_STRING:
		v.string = va_arg(ap, di_string);
		break;
	case DI_TYPE_NINT:
		v.nint = va_arg(ap, int);
		break;
	case DI_TYPE_NUINT:
		v.nuint = va_arg(ap, unsigned int);
		break;
	case DI_TYPE_INT:
		v.int_ = va_arg(ap, int64_t);
		break;
	case DI_TYPE_UINT:
		v.uint = va_arg(ap, uint64_t);
		break;
	case DI_TYPE_FLOAT:
		v.float_ = va_arg(ap, double);
		break;
	case DI_TYPE_BOOL:
		// Per C standard, bool is converted to int in va_arg
		v.bool_ = va_arg(ap, int);
		break;
	case DI_TYPE_ARRAY:
		v.array = va_arg(ap, di_array);
		break;
	case DI_TYPE_VARIANT:
		v.variant = va_arg(ap, struct di_variant);
		break;
	case DI_TYPE_TUPLE:
		v.tuple = va_arg(ap, di_tuple);
		break;
	case DI_TYPE_NIL:
	case DI_TYPE_ANY:
	case DI_LAST_TYPE:
	default:
		DI_PANIC("Trying to get value of invalid type from va_arg");
	}

	// if buf == NULL, the caller just want to pop the value
	if (buf) {
		memcpy(buf, &v, di_sizeof_type(t));
	}
}

#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))

#define toint_saturating(x)                                                              \
	_Generic((x),                                                                        \
	    int: (x),                                                                        \
	    unsigned int: (int)min(x, INT_MAX),                                              \
	    short: (int)(x),                                                                 \
	    unsigned short: (int)(x),                                                        \
	    char: (int)(x),                                                                  \
	    unsigned char: (int)(x),                                                         \
	    long: (int)min(x, INT_MAX),                                                      \
	    unsigned long: (int)min(x, INT_MAX),                                             \
	    long long: (int)min(x, INT_MAX),                                                 \
	    unsigned long long: (int)min(x, INT_MAX))
