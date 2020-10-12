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

PUBLIC_DEAI_API struct di_object *ret_nonnull di_new_error(const char *nonnull fmt, ...);

int di_gmethod(struct di_object *nonnull o, const char *nonnull name,
               void (*nonnull fn)(void)) nonnull_args(1, 2, 3);

PUBLIC_DEAI_API int di_proxy_signal(struct di_object *nonnull src, const char *nonnull srcsig,
                                    struct di_object *nonnull proxy,
                                    const char *nonnull proxysig) nonnull_args(1, 2, 3, 4);

#define DTOR(o) ((struct di_object *)(o))->dtor

#define RET_IF_ERR(expr)                                                                 \
	do {                                                                             \
		int ret = (expr);                                                        \
		if (ret != 0)                                                            \
			return ret;                                                      \
	} while (0)

#define ABRT_IF_ERR(expr)                                                                \
	do {                                                                             \
		int ret = (expr);                                                        \
		if (ret != 0)                                                            \
			abort();                                                         \
	} while (0)

#define di_set(o, prop, v)                                                               \
	({                                                                               \
		__auto_type __tmp = (v);                                                 \
		di_setx(o, prop, di_typeof(__tmp), &__tmp);                              \
	})

#define di_get(o, prop, r)                                                                      \
	({                                                                                      \
		int rc;                                                                         \
		do {                                                                            \
			rc = di_getxt((void *)(o), prop, di_typeof(r), (union di_value *)&(r)); \
			if (rc != 0) {                                                          \
				break;                                                          \
			}                                                                       \
		} while (0);                                                                    \
		rc;                                                                             \
	})

#define di_gets(o, prop, r)                                                              \
	if (di_get(o, prop, r))                                                          \
		return;

// Pardon this mess, this is what you get for doing meta programming using C macros.
#define _ARG13(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, N, ...) N
#define VA_ARGS_LENGTH(...) _ARG13(0, ##__VA_ARGS__, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
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
#define LIST_APPLY_11(fn, sep, x, ...) fn(x) sep() LIST_APPLY_10(fn, sep, __VA_ARGS__)
#define LIST_APPLY_(N, fn, sep, ...) CONCAT(LIST_APPLY_, N)(fn, sep, __VA_ARGS__)
#define LIST_APPLY(fn, sep, ...)                                                         \
	LIST_APPLY_(VA_ARGS_LENGTH(__VA_ARGS__), fn, sep, __VA_ARGS__)

#define LIST_APPLY_pre0(fn, sep, ...)
#define LIST_APPLY_pre1(fn, sep, x, ...) sep() fn(x)
#define LIST_APPLY_pre2(fn, sep, x, ...) sep() fn(x) LIST_APPLY_pre1(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre3(fn, sep, x, ...) sep() fn(x) LIST_APPLY_pre2(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre4(fn, sep, x, ...) sep() fn(x) LIST_APPLY_pre3(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre5(fn, sep, x, ...) sep() fn(x) LIST_APPLY_pre4(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre6(fn, sep, x, ...) sep() fn(x) LIST_APPLY_pre5(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre7(fn, sep, x, ...) sep() fn(x) LIST_APPLY_pre6(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre8(fn, sep, x, ...) sep() fn(x) LIST_APPLY_pre7(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre9(fn, sep, x, ...) sep() fn(x) LIST_APPLY_pre8(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre10(fn, sep, x, ...)                                                \
	sep() fn(x) LIST_APPLY_pre9(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre11(fn, sep, x, ...) sep() fn(x) LIST_APPLY_10(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre_(N, fn, sep, ...) CONCAT(LIST_APPLY_pre, N)(fn, sep, __VA_ARGS__)
#define LIST_APPLY_pre(fn, sep, ...)                                                     \
	LIST_APPLY_pre_(VA_ARGS_LENGTH(__VA_ARGS__), fn, sep, __VA_ARGS__)

#define SEP_COMMA() ,
#define SEP_COLON() ;
#define SEP_NONE()

#define STRINGIFY(x) #x

#define addressof(x) (&((typeof(x)[]){x})[0])
#define addressof_di_value(x) ((union di_value *)addressof(x))

#define di_type_pair(v) di_typeof(v), v,

#define object_cleanup __attribute__((cleanup(di_free_di_objectp)))

#define capture(...)                                                                     \
	VA_ARGS_LENGTH(__VA_ARGS__)                                                      \
	, (di_type_t[]){LIST_APPLY(di_typeof, SEP_COMMA, __VA_ARGS__)},                  \
	    (const union di_value *[]) {                                                 \
		LIST_APPLY(addressof_di_value, SEP_COMMA, __VA_ARGS__)                   \
	}

#define capture_types(...) LIST_APPLY_pre(typeof, SEP_COMMA, __VA_ARGS__)

#define di_closure(fn, caps, ...)                                                             \
	di_create_closure((void *)fn, di_return_typeid(fn capture_types caps, ##__VA_ARGS__), \
	                  capture caps, VA_ARGS_LENGTH(__VA_ARGS__),                          \
	                  (di_type_t[]){LIST_APPLY(di_typeid, SEP_COMMA, __VA_ARGS__)})

#define _di_getm(di_expr, modn, on_err)                                                  \
	object_cleanup struct di_object *modn##m = NULL;                                 \
	do {                                                                             \
		int rc = 0;                                                              \
		di_object_with_cleanup __deai_tmp_di = (struct di_object *)(di_expr);    \
		if (__deai_tmp_di == NULL) {                                             \
			on_err;                                                          \
		}                                                                        \
		struct di_object *__o;                                                   \
		rc = di_get(__deai_tmp_di, #modn, __o);                                  \
		if (rc != 0) {                                                           \
			on_err;                                                          \
		}                                                                        \
		modn##m = __o;                                                           \
	} while (0)

#define di_mgetm(mod, modn, on_err)                                                      \
	_di_getm(di_module_get_deai((struct di_module *)(mod)), modn, return (on_err))
#define di_mgetmi(mod, modn)                                                             \
	_di_getm(di_module_get_deai((struct di_module *)(mod)), modn, break)

// call but ignore return
#define di_call(o, name, ...)                                                            \
	({                                                                               \
		int rc = 0;                                                              \
		do {                                                                     \
			di_type_t rtype;                                                 \
			union di_value ret;                                              \
			bool called;                                                     \
			rc = di_callx((struct di_object *)(o), (name), &rtype, &ret,     \
			              di_tuple(__VA_ARGS__), &called);                   \
			if (rc != 0) {                                                   \
				break;                                                   \
			}                                                                \
			di_free_value(rtype, &ret);                                      \
		} while (0);                                                             \
		rc;                                                                      \
	})

#define di_callr(o, name, r, ...)                                                        \
	({                                                                               \
		int rc = 0;                                                              \
		do {                                                                     \
			di_type_t rtype;                                                 \
			union di_value ret;                                              \
			bool called;                                                     \
			rc = di_callx((struct di_object *)(o), (name), &rtype, &ret,     \
			              di_tuple(__VA_ARGS__), &called);                   \
			if (rc != 0) {                                                   \
				break;                                                   \
			}                                                                \
			if (di_typeof(r) != rtype) {                                     \
				di_free_value(rtype, &ret);                              \
				rc = -EINVAL;                                            \
				break;                                                   \
			}                                                                \
			(r) = *(typeof(r) *)&ret;                                        \
		} while (0);                                                             \
		rc;                                                                      \
	})

#define di_variant(x)                                                                    \
	((struct di_variant){                                                            \
	    (union di_value *)addressof(x),                                              \
	    di_typeof(x),                                                                \
	})
#define di_tuple(...)                                                                    \
	((struct di_tuple){                                                              \
	    VA_ARGS_LENGTH(__VA_ARGS__),                                                 \
	    (struct di_variant[]){LIST_APPLY(di_variant, SEP_COMMA, __VA_ARGS__)}})

#define di_call_callable(c, ...)                                                         \
	({                                                                               \
		int rc = 0;                                                              \
		do {                                                                     \
			di_type_t rt;                                                    \
			void *ret;                                                       \
			rc = c->call(c, &rt, &ret, di_tuple(__VA_ARGS__));               \
			if (rc != 0)                                                     \
				break;                                                   \
			di_free_value(rt, ret);                                          \
			free(ret);                                                       \
		} while (0);                                                             \
		rc;                                                                      \
	})

#define di_callr_callable(c, r, ...)                                                     \
	({                                                                               \
		int rc = 0;                                                              \
		do {                                                                     \
			di_type_t rt;                                                    \
			void *ret;                                                       \
			rc = c->call(c, &rt, &ret, di_tuple(__VA_ARGS__));               \
			if (rc != 0)                                                     \
				break;                                                   \
			if (di_typeof(r) != rt) {                                        \
				di_free_value(rt, ret);                                  \
				free(ret);                                               \
				rc = -EINVAL;                                            \
				break;                                                   \
			}                                                                \
			(r) = *(typeof(r) *)ret;                                         \
			free(ret);                                                       \
		} while (0);                                                             \
		rc;                                                                      \
	})

#define di_has_member(o, name) (di_lookup((struct di_object *)(o), name) != NULL)
#define di_emit(o, name, ...) di_emitn((struct di_object *)o, name, di_tuple(__VA_ARGS__))

/// Register a field of struct `o` as a read only member of the di_object, by using a
/// field getter
#define di_field(o, name)                                                                    \
	({                                                                                   \
		__auto_type __deai_tmp_field_getter =                                        \
		    di_new_field_getter(di_typeof((o)->name), offsetof(typeof(*(o)), name)); \
		di_member((struct di_object *)(o), "__get_" #name, __deai_tmp_field_getter); \
	})

#define di_member(o, name, v)                                                            \
	di_add_member_move((struct di_object *)(o), name, (di_type_t[]){di_typeof(v)}, &(v))

#define di_member_clone(o, name, v)                                                      \
	di_add_member_clone((struct di_object *)(o), name, di_typeof(v), (v))

#define di_getter(o, name, g) di_method(o, STRINGIFY(__get_##name), g)

#define di_getter_setter(o, name, g, s)                                                  \
	({                                                                               \
		int rc = 0;                                                              \
		do {                                                                     \
			rc = di_getter(o, name, g);                                      \
			if (rc != 0)                                                     \
				break;                                                   \
			rc = di_method(o, STRINGIFY(__set_##name), s,                    \
			               di_return_typeof(g, struct di_object *));         \
		} while (0);                                                             \
		rc;                                                                      \
	})

// Note: this is just used to create a value of `type`, but this value is not guaranteed
// to be a valid value for `type` at runtime. This is only meant to be used for compile
// time metaprogramming
#define TYPE_INIT(type)                                                                  \
	_Generic((type *)0, \
	struct di_array *: DI_ARRAY_INIT, \
	struct di_tuple *: DI_TUPLE_INIT, \
	struct di_variant *: DI_VARIANT_INIT, \
	int *: 0, \
	unsigned int *: 0, \
	int64_t *: 0, \
	uint64_t *: 0, \
	char **: NULL, \
	const char **: NULL, \
	struct di_object **: NULL, \
	struct di_weak_object **: NULL, \
	void **: NULL, \
	double *: 0.0, \
	bool *: false \
	)

#define gen_args(...) LIST_APPLY(TYPE_INIT, SEP_COMMA, ##__VA_ARGS__)

#define di_return_typeof(fn, ...) typeof(fn(gen_args(__VA_ARGS__)))
#define di_return_typeid(fn, ...) di_typeid(di_return_typeof(fn, ##__VA_ARGS__))

#define di_register_typed_method(o, name, fn, rtype, ...)                                \
	di_add_method((struct di_object *)(o), (name), (void *)(fn), (rtype),            \
	              VA_ARGS_LENGTH(__VA_ARGS__), ##__VA_ARGS__)

#define INDIRECT(fn, ...) fn(__VA_ARGS__)

// Need to use INDIRECT because macro(A B) is consider to have only one argument,
// even if B expands to something starts with a comma
#define di_method(obj, name, fn, ...)                                                    \
	INDIRECT(di_register_typed_method, obj, name, fn,                                \
	         di_return_typeid(fn, struct di_object *, ##__VA_ARGS__)                 \
	             LIST_APPLY_pre(di_typeid, SEP_COMMA, ##__VA_ARGS__))

#define generate_cleanup(object_type)                                                    \
	static inline void unused di_free_##object_type##p(                              \
	    struct object_type *nullable *nonnull p) {                                   \
		if (*p) {                                                                \
			di_unref_object((struct di_object *)*p);                         \
		}                                                                        \
		*p = NULL;                                                               \
	}

static inline void unused di_free_di_weak_objectp(struct di_weak_object *nullable *nonnull p) {
	if (*p) {
		di_drop_weak_ref(p);
	}
}

generate_cleanup(di_object);
generate_cleanup(di_closure);

#define with_di_cleanup(type) __attribute__((cleanup(di_free_##type##p))) struct type *
#define di_weak_object_with_cleanup with_di_cleanup(di_weak_object)
#define di_closure_with_cleanup with_di_cleanup(di_closure)
#define di_object_with_cleanup with_di_cleanup(di_object)

static void unused nonnull_all trivial_destroyed_handler(struct di_object *nonnull o) {
	di_destroy_object(o);
}

static inline unused const char *nonnull di_type_to_string(di_type_t type) {
#define TYPE_CASE(name)                                                                  \
	case DI_TYPE_##name:                                                             \
		return #name
	switch (type) {
		LIST_APPLY(TYPE_CASE, SEP_COLON, NIL, ANY, BOOL, INT, UINT, NINT, NUINT,
		           FLOAT, STRING, STRING_LITERAL);
		LIST_APPLY(TYPE_CASE, SEP_COLON, TUPLE, ARRAY, VARIANT, OBJECT,
		           WEAK_OBJECT, POINTER);
	case DI_LAST_TYPE:
		return "LAST_TYPE";
	}
	unreachable();
}

#define __DEAI_MEMBER_NAME "__deai"

static inline struct di_object *nullable unused di_object_get_deai_weak(struct di_object *nonnull o) {
	di_weak_object_with_cleanup weak = NULL;
	di_get(o, __DEAI_MEMBER_NAME, weak);

	if (weak == NULL) {
		return NULL;
	}
	return di_upgrade_weak_ref(weak);
}

static inline struct di_object *nullable unused di_object_get_deai_strong(struct di_object *nonnull o) {
	struct di_object *strong = NULL;
	di_get(o, __DEAI_MEMBER_NAME, strong);
	return strong;
}

/// Downgrade the __deai member from a strong reference to a weak reference
static inline void unused di_object_downgrade_deai(struct di_object *nonnull o) {
	di_object_with_cleanup di_obj = NULL;
	di_get(o, __DEAI_MEMBER_NAME, di_obj);
	if (di_obj != NULL) {
		__auto_type weak = di_weakly_ref_object(di_obj);
		di_remove_member_raw(o, __DEAI_MEMBER_NAME);
		di_member(o, __DEAI_MEMBER_NAME, weak);
	}
}
/// Upgrade the __deai member from a weak reference to a strong reference
static inline void unused di_object_upgrade_deai(struct di_object *nonnull o) {
	di_weak_object_with_cleanup di_obj = NULL;
	di_get(o, __DEAI_MEMBER_NAME, di_obj);
	if (di_obj != NULL) {
		__auto_type strong = di_upgrade_weak_ref(di_obj);
		di_remove_member_raw(o, __DEAI_MEMBER_NAME);
		if (strong != NULL) {
			di_member(o, "__deai", strong);
		}
	}
}

static inline struct di_object *nullable unused di_module_get_deai(struct di_module *nonnull o) {
	return di_object_get_deai_weak((struct di_object *)o);
}
