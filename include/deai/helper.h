/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

#include <deai/deai.h>
#include <string.h>

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int di_register_signal(struct di_object *r, const char *name, int nargs,
                       di_type_t *types);
int di_new_value_with_address(struct di_object *o, const char *name, bool writable,
                              bool own, di_type_t t, void *v);
int di_new_value(struct di_object *o, const char *name, bool writable, di_type_t t,
                 ...);
struct di_listener *
di_add_listener(struct di_object *o, const char *name, struct di_object *h);

int di_emitn_from_object(struct di_object *o, const char *name, const void *const *args);
int di_emit_from_object(struct di_object *o, const char *name, ...);
#define RET_IF_ERR(expr)                                                            \
	do {                                                                        \
		int ret = (expr);                                                   \
		if (ret != 0)                                                       \
			return ret;                                                 \
	} while (0)

#define ABRT_IF_ERR(expr)                                                           \
	do {                                                                        \
		int ret = (expr);                                                   \
		if (ret != 0)                                                       \
			abort();                                                    \
	} while (0)

#define di_set(o, prop, v)                                                          \
	({                                                                          \
		__auto_type __tmp = (v);                                            \
		di_setx(o, prop, di_typeof(__tmp), &__tmp);                         \
	})

#define di_get(o, prop, r)                                                          \
	({                                                                          \
		int rc;                                                             \
		const void *ret;                                                    \
		do {                                                                \
			rc = di_getxt((void *)o, prop, di_typeof(r), &ret);         \
			if (rc != 0)                                                \
				break;                                              \
			(r) = *(typeof(r) *)ret;                                    \
			free((void *)ret);                                          \
		} while (0);                                                        \
		rc;                                                                 \
	})

#define di_gets(o, prop, r)                                                         \
	if (di_get(o, prop, r))                                                     \
		return;

// Pardon this mess, this is what you get for doing meta programming using C macros.
#define CONCAT2(a, b) a##b
#define CONCAT1(a, b) CONCAT2(a, b)
#define CONCAT(a, b) CONCAT1(a, b)

#define _ARG13(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, N, ...) N
#define VA_ARGS_LENGTH(...)                                                         \
	_ARG13(0, ##__VA_ARGS__, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define HAS_COMMA(...) _ARG13(0, ##__VA_ARGS__, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0)

#define LIST_SHIFT_0(...) __VA_ARGS__
#define LIST_SHIFT_1(_0, ...) __VA_ARGS__
#define LIST_SHIFT_2(_0, ...) LIST_SHIFT_1(__VA_ARGS__)
#define LIST_SHIFT_3(_0, ...) LIST_SHIFT_2(__VA_ARGS__)

#define LIST_APPLY_0(fn, sep, ...)
#define LIST_APPLY_1(fn, sep, x, ...) fn(x)
#define LIST_APPLY_2(fn, sep, x, ...) fn(x) sep() LIST_APPLY_1(fn, sep, __VA_ARGS__)
#define LIST_APPLY_3(fn, sep, x, ...) fn(x) sep() LIST_APPLY_2(fn, sep, __VA_ARGS__)
#define LIST_APPLY_4(fn, sep, x, ...) fn(x) sep() LIST_APPLY_3(fn, sep, __VA_ARGS__)
#define LIST_APPLY_5(fn, sep, x, ...) fn(x) sep() LIST_APPLY_4(fn, sep, __VA_ARGS__)
#define LIST_APPLY_6(fn, sep, x, ...) fn(x) sep() LIST_APPLY_5(fn, sep, __VA_ARGS__)
#define LIST_APPLY_7(fn, sep, x, ...) fn(x) sep() LIST_APPLY_6(fn, sep, __VA_ARGS__)
#define LIST_APPLY_8(fn, sep, x, ...) fn(x) sep() LIST_APPLY_7(fn, sep, __VA_ARGS__)
#define LIST_APPLY_9(fn, sep, x, ...) fn(x) sep() LIST_APPLY_8(fn, sep, __VA_ARGS__)
#define LIST_APPLY_10(fn, sep, x, ...) fn(x) sep() LIST_APPLY_9(fn, sep, __VA_ARGS__)
#define LIST_APPLY_11(fn, sep, x, ...)                                              \
	fn(x) sep() LIST_APPLY_10(fn, sep, __VA_ARGS__)
#define LIST_APPLY_(N, fn, sep, ...) CONCAT(LIST_APPLY_, N)(fn, sep, __VA_ARGS__)
#define LIST_APPLY(fn, sep, ...)                                                    \
	LIST_APPLY_(VA_ARGS_LENGTH(__VA_ARGS__), fn, sep, __VA_ARGS__)

#define LIST_APPLY_pre0(fn, sep, ...)
#define LIST_APPLY_pre1(fn, sep, x, ...) sep() fn(x)
#define LIST_APPLY_pre2(fn, sep, x, ...)                                            \
	sep() fn(x) LIST_APPLY_pre1(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre3(fn, sep, x, ...)                                            \
	sep() fn(x) LIST_APPLY_pre2(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre4(fn, sep, x, ...)                                            \
	sep() fn(x) LIST_APPLY_pre3(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre5(fn, sep, x, ...)                                            \
	sep() fn(x) LIST_APPLY_pre4(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre6(fn, sep, x, ...)                                            \
	sep() fn(x) LIST_APPLY_pre5(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre7(fn, sep, x, ...)                                            \
	sep() fn(x) LIST_APPLY_pre6(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre8(fn, sep, x, ...)                                            \
	sep() fn(x) LIST_APPLY_pre7(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre9(fn, sep, x, ...)                                            \
	sep() fn(x) LIST_APPLY_pre8(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre10(fn, sep, x, ...)                                           \
	sep() fn(x) LIST_APPLY_pre9(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre11(fn, sep, x, ...)                                           \
	sep() fn(x) LIST_APPLY_10(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre_(N, fn, sep, ...)                                            \
	CONCAT(LIST_APPLY_pre, N)(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre(fn, sep, ...)                                                \
	LIST_APPLY_pre_(VA_ARGS_LENGTH(__VA_ARGS__), fn, sep, __VA_ARGS__)

#define SEP_COMMA() ,
#define SEP_COLON() ;
#define SEP_NONE()

#define STRINGIFY(x) #x

#define di_type_pair(v) di_typeof(v), v,
#define di_arg_list(...) LIST_APPLY(di_type_pair, SEP_NONE, __VA_ARGS__) DI_LAST_TYPE

#define object_cleanup __attribute__((cleanup(__free_objp)))

#define di_getm(di, modn, on_err)                                                   \
	object_cleanup struct di_object *modn##m = NULL;                            \
	do {                                                                        \
		struct di_object *__o;                                              \
		int rc = di_get(di, #modn, __o);                                    \
		if (rc != 0)                                                        \
			return (on_err);                                            \
		if (!di_check_type(__o, "module")) {                                \
			rc = -EINVAL;                                               \
			return (on_err);                                            \
		}                                                                   \
		modn##m = __o;                                                      \
	} while (0);

// call but ignore return
#define di_call(o, name, ...)                                                       \
	({                                                                          \
		int rc = 0;                                                         \
		do {                                                                \
			di_type_t rtype;                                            \
			void *ret;                                                  \
			rc = di_callx((struct di_object *)(o), (name), &rtype,      \
			              &ret, di_arg_list(__VA_ARGS__));              \
			if (rc != 0)                                                \
				break;                                              \
			di_free_value(rtype, (void *)ret);                          \
			free((void *)ret);                                          \
		} while (0);                                                        \
		rc;                                                                 \
	})

#define di_callr(o, name, r, ...)                                                   \
	({                                                                          \
		int rc = 0;                                                         \
		do {                                                                \
			di_type_t rtype;                                            \
			void *ret;                                                  \
			rc = di_callx((struct di_object *)(o), (name), &rtype,      \
			              &ret, di_arg_list(__VA_ARGS__));              \
			if (rc != 0)                                                \
				break;                                              \
			if (di_typeof(r) != rtype) {                                \
				di_free_value(rtype, (void *)ret);                  \
				free((void *)ret);                                  \
				rc = -EINVAL;                                       \
				break;                                              \
			}                                                           \
			(r) = *(typeof(r) *)ret;                                    \
			free(ret);                                                  \
		} while (0);                                                        \
		rc;                                                                 \
	})

#define di_field(o, name)                                                           \
	di_register_field_getter((struct di_object *)o, STRINGIFY(__get_##name),    \
	                         offsetof(typeof(*o), name), di_typeof(o->name))

#define di_signal_handler(o, name, add, del)                                        \
	do {                                                                        \
		di_register_typed_method((struct di_object *)o, (di_fn_t)add,       \
		                         "__add_listener_" name, DI_TYPE_VOID, 0);  \
		di_register_typed_method((struct di_object *)o, (di_fn_t)del,       \
		                         "__del_listener_" name, DI_TYPE_VOID, 0);  \
	} while (0)

#define TYPE_INIT(type)                                                             \
	_Generic((type *)0, \
	struct di_array *: DI_ARRAY_NIL, \
	int *: 0, \
	unsigned int *: 0, \
	int64_t *: 0, \
	uint64_t *: 0, \
	char **: NULL, \
	const char **: NULL, \
	struct di_object **: NULL, \
	void **: NULL, \
	double *: 0.0 \
	)
#define gen_args(...) LIST_APPLY(TYPE_INIT, SEP_COMMA, __VA_ARGS__)

#define di_return_typeof(fn, ...) di_typeid(typeof(fn(gen_args(__VA_ARGS__))))

#define di_register_typed_method(o, name, fn, rtype, ...)                           \
	di_add_method((void *)(o), (name), (void *)(fn), (rtype),                   \
	              VA_ARGS_LENGTH(__VA_ARGS__), ##__VA_ARGS__)

#define INDIRECT(fn, ...) fn(__VA_ARGS__)

// Need to use INDIRECT because macro(A B) is consider to have only one argument,
// even if B expands to something starts with a comma
#define di_method(obj, name, fn, ...)                                               \
	INDIRECT(di_register_typed_method, (struct di_object *)obj, name,           \
	         (di_fn_t)fn,                                                       \
	         di_return_typeof(fn, struct di_object *, ##__VA_ARGS__)            \
	             LIST_APPLY_pre(di_typeid, SEP_COMMA, ##__VA_ARGS__))

static inline void __free_objp(struct di_object **p) {
	if (*p)
		di_unref_object(*p);
	*p = NULL;
}

// TODO maybe
// macro to generate c wrapper for di functions
