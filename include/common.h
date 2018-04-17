/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

#include <deai/compiler.h>

#define tmalloc(type, nmem) (type *)calloc(nmem, sizeof(type))
#define auto __auto_type

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member)                                             \
	__extension__({                                                             \
		const typeof(((type *)0)->member) *__mptr = (ptr);                  \
		(type *)((char *)__mptr - offsetof(type, member));                  \
	})

#define define_trivial_cleanup(type, name)                                          \
	static inline unused void name(type **ptr) {                                \
		free(*ptr);                                                         \
		*ptr = NULL;                                                        \
	}
#define define_trivial_cleanup_t(type) define_trivial_cleanup(type, free_##type##p)

#define with_cleanup_t(type) __attribute__((cleanup(free_##type##p))) type *
#define with_cleanup(func) __attribute__((cleanup(func)))

#define IS_BIG_ENDIAN (!*(unsigned char *)&(uint16_t){1})

#define CONCAT2(a, b) a##b
#define CONCAT1(a, b) CONCAT2(a, b)
#define CONCAT(a, b) CONCAT1(a, b)

#ifdef __clang__
#if __has_extension(blocks)
__attribute__((weak)) long _NSConcreteGlobalBlock;
static void unused __clang_cleanup_func(void (^*dfunc)(void)) {
    (*dfunc)();
}

#define defer                                        \
    void (^CONCAT(__defer_f_, __COUNTER__))(void) \
        __attribute__((cleanup(__clang_cleanup_func))) unused = ^

#else
#define defer _Static_assert(false, "no blocks support in clang");
#endif
#else

#define _DEFER(a, count)                                                                      \
    void CONCAT(__defer_f_, count)(void* _defer_arg unused);         \
    int CONCAT(__defer_var_, count) __attribute__((cleanup(CONCAT(__defer_f_, count)))) \
        unused;                                                              \
    void CONCAT(__defer_f_, count)(void* _defer_arg unused)
#define defer _DEFER(a, __COUNTER__)
#endif
