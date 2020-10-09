/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <deai/compiler.h>
#include <deai/object.h>
#include "common.h"

static inline void typed_alloc_copy(di_type_t type, const void *src, void **dest) {
	void *ret = calloc(1, di_sizeof_type(type));
	memcpy(ret, src, di_sizeof_type(type));
	*dest = ret;
}

static inline int integer_conversion(di_type_t inty, const void *inp, di_type_t outty,
                                     void **outp, bool *cloned) {
	*cloned = false;
	if (inty == outty) {
		*outp = (void *)inp;
		return 0;
	}

#define convert_case(srct, dstt, dstmax, dstmin)                                         \
	case di_typeof((srct)0):                                                         \
		do {                                                                     \
			srct tmp = *(srct *)(inp);                                       \
			if (tmp > (dstmax) || tmp < (dstmin)) {                          \
				*outp = NULL;                                            \
				return -ERANGE;                                          \
			}                                                                \
			dstt *tmp2 = malloc(sizeof(dstt));                               \
			*tmp2 = (dstt)tmp;                                               \
			*outp = tmp2;                                                    \
		} while (0);                                                             \
		break

#define convert_switch(s1, s2, s3, ...)                                                  \
	switch (inty) {                                                                  \
		convert_case(s1, __VA_ARGS__);                                           \
		convert_case(s2, __VA_ARGS__);                                           \
		convert_case(s3, __VA_ARGS__);                                           \
	case di_typeof((VA_ARG_HEAD(__VA_ARGS__))0):                                     \
		unreachable();                                                           \
	case DI_TYPE_ANY:                                                                \
	case DI_TYPE_NIL:                                                                \
	case DI_TYPE_FLOAT:                                                              \
	case DI_TYPE_BOOL:                                                               \
	case DI_TYPE_ARRAY:                                                              \
	case DI_TYPE_TUPLE:                                                              \
	case DI_TYPE_VARIANT:                                                            \
	case DI_TYPE_OBJECT:                                                             \
	case DI_TYPE_WEAK_OBJECT:                                                        \
	case DI_TYPE_STRING:                                                             \
	case DI_TYPE_STRING_LITERAL:                                                     \
	case DI_TYPE_POINTER:                                                            \
	case DI_LAST_TYPE:                                                               \
	default:                                                                         \
		*outp = NULL;                                                            \
		return -EINVAL;                                                          \
	}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wtautological-constant-out-of-range-compare"
	switch (outty) {
	case DI_TYPE_INT:
		convert_switch(unsigned int, int, uint64_t, int64_t, INT64_MAX, INT64_MIN);
		break;
	case DI_TYPE_NINT:
		convert_switch(unsigned int, uint64_t, int64_t, int, INT_MAX, INT_MIN);
		break;
	case DI_TYPE_UINT:
		convert_switch(unsigned int, int, int64_t, uint64_t, UINT64_MAX, 0);
		break;
	case DI_TYPE_NUINT:
		convert_switch(int, int64_t, uint64_t, unsigned int, UINT_MAX, 0);
		break;
	case DI_TYPE_ANY:
	case DI_TYPE_NIL:
	case DI_TYPE_FLOAT:
	case DI_TYPE_BOOL:
	case DI_TYPE_ARRAY:
	case DI_TYPE_TUPLE:
	case DI_TYPE_VARIANT:
	case DI_TYPE_OBJECT:
	case DI_TYPE_WEAK_OBJECT:
	case DI_TYPE_STRING:
	case DI_TYPE_STRING_LITERAL:
	case DI_TYPE_POINTER:
	case DI_LAST_TYPE:
	default:
		*outp = NULL;
		return -EINVAL;
	}
#pragma GCC diagnostic pop

#undef convert_case
#undef convert_switch
	*cloned = true;
	return 0;
}

static inline bool is_integer(di_type_t t) {
	return t == DI_TYPE_INT || t == DI_TYPE_NINT || t == DI_TYPE_UINT || t == DI_TYPE_NUINT;
}

/// Convert value `inp` to type `outty`. `*outp != inp` if and only if a conversion
/// happened. And if a conversion did happen, it's safe to free the original value.
///
/// @param[out] cloned if false, value in `outp` is borrowed from `inp`. otherwise the
///                    value is cloned. always false in case of an error
static inline int unused di_type_conversion(di_type_t inty, const union di_value *inp,
                                            di_type_t outty, void **outp, bool *cloned) {
	*cloned = false;
	if (inty == outty) {
		*outp = (void *)inp;
		return 0;
	}

	if (outty == DI_TYPE_VARIANT) {
		auto var = tmalloc(struct di_variant, 1);
		var->type = inty;
		var->value = malloc(sizeof(di_sizeof_type(inty)));
		di_copy_value(inty, var->value, inp);
		*cloned = true;
		return 0;
	}

	if (inty == DI_TYPE_STRING && outty == DI_TYPE_STRING_LITERAL) {
		*outp = (void *)inp;
		return 0;
	}

	if (outty == DI_TYPE_NIL) {
		*outp = NULL;
		return 0;
	}

	if (inty == DI_TYPE_VARIANT) {
		return di_type_conversion(inp->variant.type, inp->variant.value, outty,
		                          outp, cloned);
	}

	if (inty == DI_TYPE_STRING_LITERAL && outty == DI_TYPE_STRING) {
		const char **res = malloc(sizeof(const char *));
		*res = strdup(*(const char **)inp);
		*outp = res;
		*cloned = true;
		return 0;
	}

	if (is_integer(inty)) {
		if (is_integer(outty)) {
			return integer_conversion(inty, inp, outty, outp, cloned);
		}
		if (outty == DI_TYPE_FLOAT) {
			double *res = malloc(sizeof(double));
#define convert_case(srct)                                                               \
	case di_typeof((srct)0):                                                         \
		*res = (double)*(srct *)inp;                                             \
		break;
			switch (inty) {
				convert_case(unsigned int);
				convert_case(int);
				convert_case(uint64_t);
				convert_case(int64_t);
			case DI_TYPE_ANY:
			case DI_TYPE_NIL:
			case DI_TYPE_FLOAT:
			case DI_TYPE_BOOL:
			case DI_TYPE_ARRAY:
			case DI_TYPE_TUPLE:
			case DI_TYPE_VARIANT:
			case DI_TYPE_OBJECT:
			case DI_TYPE_WEAK_OBJECT:
			case DI_TYPE_STRING:
			case DI_TYPE_STRING_LITERAL:
			case DI_TYPE_POINTER:
			case DI_LAST_TYPE:
			default:
				free(res);
				*outp = NULL;
				return -EINVAL;
			}
#undef convert_case
			*outp = res;
			*cloned = true;
			return 0;
		}
	}

	// float -> integer not allowed
	*outp = NULL;
	return -EINVAL;
}

/// Fetch a value based on di_type from va_arg, and put it into `buf` if `buf` is not
/// NULL. This function only borrows the value, without cloning it.
static inline void unused va_arg_with_di_type(va_list ap, di_type_t t, void *buf) {
	union di_value v;

	switch (t) {
	case DI_TYPE_STRING:
	case DI_TYPE_STRING_LITERAL:
	case DI_TYPE_POINTER:
	case DI_TYPE_OBJECT:
	case DI_TYPE_WEAK_OBJECT:
		v.pointer = va_arg(ap, void *);
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
		v.array = va_arg(ap, struct di_array);
		break;
	case DI_TYPE_VARIANT:
		v.variant = va_arg(ap, struct di_variant);
		break;
	case DI_TYPE_TUPLE:
		v.tuple = va_arg(ap, struct di_tuple);
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
