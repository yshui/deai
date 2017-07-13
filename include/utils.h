/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>

#include "common.h"

static inline void
typed_alloc_copy(di_type_t type, const void **dest, const void *src) {
	void *ret = calloc(1, di_sizeof_type(type));
	memcpy(ret, src, di_sizeof_type(type));
	*dest = ret;
}

static inline int integer_conversion(di_type_t inty, const void *inp,
                                     di_type_t outty, const void **outp) {
	if (inty == outty) {
		typed_alloc_copy(inty, outp, inp);
		return 0;
	}

#define convert_case(srct, dstt, dstmax, dstmin)                                    \
	case di_typeof((srct)0):                                                    \
		do {                                                                \
			srct tmp = *(srct *)(inp);                                  \
			if (tmp > (dstmax) || tmp < (dstmin)) {                     \
				*outp = NULL;                                       \
				return -ERANGE;                                     \
			}                                                           \
			dstt *tmp2 = malloc(sizeof(dstt));                          \
			*tmp2 = (dstt)tmp;                                          \
			*outp = tmp2;                                               \
		} while (0);                                                        \
		break

#define convert_switch(s1, s2, s3, ...)                                             \
	switch (inty) {                                                             \
		convert_case(s1, __VA_ARGS__);                                      \
		convert_case(s2, __VA_ARGS__);                                      \
		convert_case(s3, __VA_ARGS__);                                      \
	default: *outp = NULL; return -EINVAL;                                      \
	}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wtautological-constant-out-of-range-compare"
	switch (outty) {
	case DI_TYPE_INT:
		convert_switch(unsigned int, int, uint64_t, int64_t, INT64_MAX,
		               INT64_MIN);
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
	default: *outp = NULL; return -EINVAL;
	}
#pragma GCC diagnostic pop

#undef convert_case
#undef convert_switch
	return 0;
}

static inline bool is_integer(di_type_t t) {
	return t == DI_TYPE_INT || t == DI_TYPE_NINT || t == DI_TYPE_UINT ||
	       t == DI_TYPE_NUINT;
}

static inline int di_type_conversion(di_type_t inty, const void *inp,
                                     di_type_t outty, const void **outp) {
	if (inty == outty) {
		typed_alloc_copy(inty, outp, inp);
		return 0;
	}

	if (inty == DI_TYPE_STRING_LITERAL && outty == DI_TYPE_STRING) {
		const char **res = malloc(sizeof(const char *));
		*res = strdup(*(const char **)inp);
		*outp = res;
		return 0;
	}

	if (is_integer(inty)) {
		if (is_integer(outty))
			return integer_conversion(inty, inp, outty, outp);
		if (outty == DI_TYPE_FLOAT) {
			double *res = malloc(sizeof(double));
#define convert_case(srct)                                                          \
	case di_typeof((srct)0): *res = (double)*(srct *)inp; break;
			switch (inty) {
				convert_case(unsigned int);
				convert_case(int);
				convert_case(uint64_t);
				convert_case(int64_t);
			default:
				free(res);
				*outp = NULL;
				return -EINVAL;
			}
#undef convert_case
			*outp = res;
			return 0;
		}
	}

	// float -> integer not allowed
	*outp = NULL;
	return -EINVAL;
}

static inline void va_arg_with_di_type(va_list ap, di_type_t t, void *buf) {
	void *ptr, *src = NULL;
	int64_t i64;
	uint64_t u64;
	int ni;
	unsigned int nui;
	double d;

	switch (t) {
	case DI_TYPE_STRING:
	case DI_TYPE_STRING_LITERAL:
	case DI_TYPE_POINTER:
	case DI_TYPE_OBJECT:
		ptr = va_arg(ap, void *);
		src = &ptr;
		break;
	case DI_TYPE_NINT:
		ni = va_arg(ap, int);
		src = &ni;
		break;
	case DI_TYPE_NUINT:
		nui = va_arg(ap, unsigned int);
		src = &nui;
		break;
	case DI_TYPE_INT:
		i64 = va_arg(ap, int64_t);
		src = &i64;
		break;
	case DI_TYPE_UINT:
		u64 = va_arg(ap, uint64_t);
		src = &u64;
		break;
	case DI_TYPE_FLOAT:
		d = va_arg(ap, double);
		src = &d;
		break;
	default: assert(0);
	}

	// if buf == NULL, the caller just want to pop the value
	if (buf)
		memcpy(buf, src, di_sizeof_type(t));
}
