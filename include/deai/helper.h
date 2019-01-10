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

#define CONCAT2(a, b) a##b
#define CONCAT1(a, b) CONCAT2(a, b)
#define CONCAT(a, b) CONCAT1(a, b)

struct di_object *ret_nonnull di_new_error(const char *nonnull fmt, ...);

int di_gmethod(struct di_object *nonnull o, const char *nonnull name,
               void (*nonnull fn)(void)) nonnull_args(1, 2, 3);

int di_proxy_signal(struct di_object *nonnull src, const char *nonnull srcsig,
                    struct di_object *nonnull proxy, const char *nonnull proxysig)
    nonnull_args(1, 2, 3, 4);

#define DTOR(o) ((struct di_object *)o)->dtor

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
		void *ret;                                                          \
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

#define addressof(x) (&((typeof(x)[]){x}))

#define di_type_pair(v) di_typeof(v), v,
#define di_arg_list(...) LIST_APPLY(di_type_pair, SEP_NONE, __VA_ARGS__) DI_LAST_TYPE

#define object_cleanup __attribute__((cleanup(__free_objp)))

#define capture(...)                                                                \
	VA_ARGS_LENGTH(__VA_ARGS__)                                                 \
	, (di_type_t[]){LIST_APPLY(di_typeof, SEP_COMMA, __VA_ARGS__)},             \
	    (const void *[]) {                                                      \
		LIST_APPLY(addressof, SEP_COMMA, __VA_ARGS__)                       \
	}

#define capture_types(...) LIST_APPLY_pre(typeof, SEP_COMMA, __VA_ARGS__)

#define di_closure(fn, weak, caps, ...)                                             \
	di_create_closure(                                                          \
	    (void *)fn, di_return_typeid(fn capture_types caps, ##__VA_ARGS__),     \
	    capture caps, VA_ARGS_LENGTH(__VA_ARGS__),                              \
	    (di_type_t[]){LIST_APPLY(di_typeid, SEP_COMMA, __VA_ARGS__)}, weak)

#define _di_getm(di, modn, on_err)                                                  \
	object_cleanup struct di_object *modn##m = NULL;                            \
	do {                                                                        \
		struct di_object *__o;                                              \
		int rc = di_get(di, #modn, __o);                                    \
		if (rc != 0)                                                        \
			on_err;                                                     \
		if (!di_check_type(__o, "deai:module")) {                           \
			rc = -EINVAL;                                               \
			on_err;                                                     \
		}                                                                   \
		modn##m = __o;                                                      \
	} while (0)

#define di_getm(di, modn, on_err) _di_getm(di, modn, return (on_err))
#define di_getmi(di, modn) _di_getm(di, modn, break)

#define di_schedule_call(di, fn, cap)                                               \
	do {                                                                        \
		di_getmi(di, event);                                                \
		assert(eventm);                                                     \
		auto cl = di_closure(fn, false, cap);                               \
		di_listen_to_once(eventm, "prepare", (void *)cl, true);             \
		di_unref_object((void *)cl);                                        \
	} while (0)

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
				di_free_value(rtype, ret);                          \
				free(ret);                                          \
				rc = -EINVAL;                                       \
				break;                                              \
			}                                                           \
			(r) = *(typeof(r) *)ret;                                    \
			free(ret);                                                  \
		} while (0);                                                        \
		rc;                                                                 \
	})

#define di_tuple(...)                                                               \
	((struct di_tuple){                                                         \
	    VA_ARGS_LENGTH(__VA_ARGS__),                                            \
	    (void *[]){LIST_APPLY(addressof, SEP_COMMA, __VA_ARGS__)},              \
	    (di_type_t[]){LIST_APPLY(di_typeof, SEP_COMMA, __VA_ARGS__)}})

#define di_call_callable(c, ...)                                                    \
	({                                                                          \
		int rc = 0;                                                         \
		do {                                                                \
			di_type_t rt;                                               \
			void *ret;                                                  \
			rc = c->call(c, &rt, &ret, di_tuple(__VA_ARGS__));          \
			if (rc != 0)                                                \
				break;                                              \
			di_free_value(rt, ret);                                     \
			free(ret);                                                  \
		} while (0);                                                        \
		rc;                                                                 \
	})

#define di_callr_callable(c, r, ...)                                                \
	({                                                                          \
		int rc = 0;                                                         \
		do {                                                                \
			di_type_t rt;                                               \
			void *ret;                                                  \
			rc = c->call(c, &rt, &ret, di_tuple(__VA_ARGS__));          \
			if (rc != 0)                                                \
				break;                                              \
			if (di_typeof(r) != rt) {                                   \
				di_free_value(rt, ret);                             \
				free(ret);                                          \
				rc = -EINVAL;                                       \
				break;                                              \
			}                                                           \
			(r) = *(typeof(r) *)ret;                                    \
			free(ret);                                                  \
		} while (0);                                                        \
		rc;                                                                 \
	})

#define di_emit(o, name, ...)                                                       \
	di_emitn((struct di_object *)o, name, di_tuple(__VA_ARGS__))

#define _di_field(o, name, w)                                                       \
	di_add_member_ref((struct di_object *)(o), #name, w, di_typeof((o)->name),  \
	                  &((o)->name))

/// Register a field of struct `o` as a read only member of the di_object
#define di_field(o, name) _di_field(o, name, false)

/// Register a field of struct `o` as a writable member of the di_object
#define di_fieldw(o, name) _di_field(o, name, true)

#define di_member(o, name, v)                                                       \
	di_add_member_move((struct di_object *)(o), name, false,                    \
	                   (di_type_t[]){di_typeof(v)}, &v)

#define di_getter(o, name, g) di_method(o, STRINGIFY(__get_##name), g)

#define di_getter_setter(o, name, g, s)                                             \
	({                                                                          \
		int rc = 0;                                                         \
		do {                                                                \
			rc = di_getter(o, name, g);                                 \
			if (rc != 0)                                                \
				break;                                              \
			rc = di_method(o, STRINGIFY(__set_##name), s,               \
			               di_return_typeof(g, struct di_object *));    \
		} while (0);                                                        \
		rc;                                                                 \
	})

#define TYPE_INIT(type)                                                             \
	_Generic((type *)0, \
	struct di_array *: DI_ARRAY_NIL, \
	struct di_tuple *: DI_TUPLE_NIL, \
	int *: 0, \
	unsigned int *: 0, \
	int64_t *: 0, \
	uint64_t *: 0, \
	char **: NULL, \
	const char **: NULL, \
	struct di_object **: NULL, \
	void **: NULL, \
	double *: 0.0, \
	bool *: false \
	)
#define gen_args(...) LIST_APPLY(TYPE_INIT, SEP_COMMA, ##__VA_ARGS__)

#define di_return_typeof(fn, ...) typeof(fn(gen_args(__VA_ARGS__)))
#define di_return_typeid(fn, ...) di_typeid(di_return_typeof(fn, ##__VA_ARGS__))

#define di_register_typed_method(o, name, fn, rtype, ...)                           \
	di_add_method((struct di_object *)(o), (name), (void *)(fn), (rtype),       \
	              VA_ARGS_LENGTH(__VA_ARGS__), ##__VA_ARGS__)

#define INDIRECT(fn, ...) fn(__VA_ARGS__)

// Need to use INDIRECT because macro(A B) is consider to have only one argument,
// even if B expands to something starts with a comma
#define di_method(obj, name, fn, ...)                                               \
	INDIRECT(di_register_typed_method, obj, name, fn,                           \
	         di_return_typeid(fn, struct di_object *, ##__VA_ARGS__)            \
	             LIST_APPLY_pre(di_typeid, SEP_COMMA, ##__VA_ARGS__))

#define MAKE_VARIANT(x) di_make_variant(di_typeid(x), x)

#define MAKE_VARIANT_LIST(...) LIST_APPLY(MAKE_VARIANT, SEP_COMMA, ##__VA_ARGS__)

#define di_make_tuple(...)                                                          \
	((struct di_tuple){VA_ARGS_LENGTH(__VA_ARGS__),                             \
	                   (struct di_variant[]){MAKE_VARIANT_LIST(__VA_ARGS__)}})

static inline void nonnull_args(1) __free_objp(struct di_object *nullable *nonnull p) {
	if (*p)
		di_unref_object(*p);
	*p = NULL;
}

static void unused nonnull_all trivial_destroyed_handler(struct di_object *nonnull o) {
	di_destroy_object(o);
}

static void unused nonnull_all trivial_detach(struct di_object *nonnull o) {
	di_destroy_object(o);
}

typedef void (*nonnull di_detach_fn_t)(struct di_object *nonnull);

static inline int nonnull_all di_set_detach(struct di_listener *nonnull l,
                                            di_detach_fn_t fn,
                                            struct di_object *nonnull o) {
	struct di_closure *cl = di_closure(fn, true, (o));
	int ret = di_add_member_clone((struct di_object *)l, "__detach", false,
	                              DI_TYPE_OBJECT, cl);
	di_unref_object((void *)cl);
	return ret;
}

static inline struct di_listener *ret_nonnull nonnull_all di_listen_to_destroyed(
    struct di_object *nonnull o, di_detach_fn_t fn, struct di_object *nonnull o2) {
	struct di_listener *ret = di_listen_to(o, "__destroyed", NULL);
	di_set_detach(ret, fn, o2);
	return ret;
}
