/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

#include <deai/compiler.h>
#include <stdio.h>
#include <stdlib.h>

#define tmalloc(type, nmem) (type *)calloc(nmem, sizeof(type))
#define trealloc(ptr, nmem)                                                              \
	({                                                                                   \
		/* NOLINTNEXTLINE(bugprone-sizeof-expression) */                                 \
		void *__trealloc_tmp = realloc(ptr, sizeof(*(ptr)) * (nmem));                    \
		DI_CHECK(__trealloc_tmp, "Out of memory");                                       \
		(typeof(ptr))__trealloc_tmp;                                                     \
	})
#ifndef __cplusplus
#define auto __auto_type
#else
#define __auto_type auto
#endif

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member)                                                  \
	__extension__({                                                                      \
		const typeof(((type *)0)->member) *__mptr = (ptr);                               \
		(type *)((char *)__mptr - offsetof(type, member));                               \
	})

#define _define_trivial_cleanup(type, name)                                              \
	static inline unused void name(type **ptr) {                                         \
		free(*ptr);                                                                      \
		*ptr = NULL;                                                                     \
	}
#define define_trivial_cleanup(type) _define_trivial_cleanup(type, di_free_##type##pp)

define_trivial_cleanup(char);

/// Check if `expr` is true, panic with `msg` if not.
#define DI_CHECK(expr, ...)                                                              \
	do {                                                                                 \
		__auto_type __di_check_tmp = (expr);                                             \
		if (!__di_check_tmp) {                                                           \
			fprintf(stderr, "Check \"" #expr "\" failed in %s at " __FILE__ ":%d. %s\n", \
			        __func__, __LINE__, #__VA_ARGS__);                                   \
			abort();                                                                     \
			unreachable();                                                               \
		}                                                                                \
	} while (0)

/// Check if function returns success (i.e. 0)
#define DI_CHECK_OK(expr, ...)                                                               \
	do {                                                                                     \
		__auto_type __di_check_tmp = (expr);                                                 \
		if (__di_check_tmp != 0) {                                                           \
			fprintf(stderr, "\"" #expr "\" failed in %s at " __FILE__ ":%d (%d != 0). %s\n", \
			        __func__, __LINE__, __di_check_tmp, #__VA_ARGS__);                       \
			abort();                                                                         \
			unreachable();                                                                   \
		}                                                                                    \
	} while (0)

/// Panic the program with `msg`
#define DI_PANIC(...)                                                                    \
	do {                                                                                 \
		DI_CHECK(false, ##__VA_ARGS__);                                                  \
		unreachable();                                                                   \
	} while (0)

#ifdef NDEBUG
#define DI_ASSERT(expr, msg)
#else
/// Like DI_CHECK, but only enabled in debug builds
#define DI_ASSERT(expr, ...) DI_CHECK(expr, ##__VA_ARGS__)
#endif

#define DI_OK_OR_RET(expr)                                                               \
	do {                                                                                 \
		__auto_type __di_ok_or_ret_tmp = (expr);                                         \
		if (__di_ok_or_ret_tmp != 0) {                                                   \
			return __di_ok_or_ret_tmp;                                                   \
		}                                                                                \
	} while (0)

#define DI_OK_OR_RET_PTR(expr)                                                           \
	do {                                                                                 \
		__auto_type __di_ok_or_ret_tmp = (expr);                                         \
		if (__di_ok_or_ret_tmp != 0) {                                                   \
			return ERR_PTR(__di_ok_or_ret_tmp);                                          \
		}                                                                                \
	} while (0)

#if defined(__LP64__) && __LP64__
#define PTR_POISON ((void *)0xffffffc01dcaffee)
#else
#define PTR_POISON ((void *)0xc01dcafe)
#endif

#define VA_ARG_HEAD(x, ...) x

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
