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
#include <stdio.h>

#define CONCAT2(a, b) a##b
#define CONCAT1(a, b) CONCAT2(a, b)
#define CONCAT(a, b) CONCAT1(a, b)

/// Create a setter that, when called, sets member `theirs` of `them` instead
PUBLIC_DEAI_API di_object *nonnull di_redirected_setter(struct di_weak_object *nonnull them,
                                                               di_string theirs);
/// Create a getter that, when called, returns member `theirs` from `them`
PUBLIC_DEAI_API di_object *nonnull di_redirected_getter(struct di_weak_object *nonnull them,
                                                               di_string theirs);
/// Redirect listeners of `ours` on `us` to `theirs` on `them`. Whenever handlers are
/// registered for `ours` on `us`, they will be redirected to `theirs` on `them` instead,
/// by adding a getter/setter for __signal_<ours> on `us`.
PUBLIC_DEAI_API int
di_redirect_signal(di_object *nonnull us, struct di_weak_object *nonnull them,
                   di_string ours, di_string theirs);

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

#define di_get(o, prop, r)                                                               \
	({                                                                               \
		int rc;                                                                  \
		do {                                                                     \
			rc = di_getxt((void *)(o), di_string_borrow(prop), di_typeof(r), \
			              (di_value *)&(r));                           \
			if (rc != 0) {                                                   \
				break;                                                   \
			}                                                                \
		} while (0);                                                             \
		rc;                                                                      \
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
#define addressof_di_value(x) ((di_value *)addressof(x))

#define di_type_pair(v) di_typeof(v), v,

#define object_cleanup __attribute__((cleanup(di_free_di_objectp)))

#define capture(...)                                                                     \
	VA_ARGS_LENGTH(__VA_ARGS__)                                                      \
	, (di_type[]){LIST_APPLY(di_typeof, SEP_COMMA, __VA_ARGS__)},                    \
	    (const di_value *[]) {                                                 \
		LIST_APPLY(addressof_di_value, SEP_COMMA, __VA_ARGS__)                   \
	}

#define capture_types(...) LIST_APPLY_pre(typeof, SEP_COMMA, __VA_ARGS__)

#define di_closure(fn, caps, ...)                                                             \
	di_create_closure((void *)fn, di_return_typeid(fn capture_types caps, ##__VA_ARGS__), \
	                  di_tuple caps, VA_ARGS_LENGTH(__VA_ARGS__),                         \
	                  (di_type[]){LIST_APPLY(di_typeid, SEP_COMMA, __VA_ARGS__)})

#define di_getm(di_expr, modn, on_err)                                                   \
	scoped_di_object *modn##m = NULL;                                                \
	do {                                                                             \
		int rc = 0;                                                              \
		scoped_di_object *__deai_tmp_di = (di_object *)(di_expr);         \
		if (__deai_tmp_di == NULL) {                                             \
			on_err;                                                          \
		}                                                                        \
		di_object *__o;                                                   \
		rc = di_get(__deai_tmp_di, #modn, __o);                                  \
		if (rc != 0) {                                                           \
			on_err;                                                          \
		}                                                                        \
		modn##m = __o;                                                           \
	} while (0)

#define di_mgetm(mod, modn, on_err)                                                      \
	di_getm(di_module_get_deai((struct di_module *)(mod)), modn, return (on_err))
#define di_mgetmi(mod, modn)                                                             \
	di_getm(di_module_get_deai((struct di_module *)(mod)), modn, break)

// call but ignore return
#define di_call(o, name, ...)                                                            \
	({                                                                               \
		int rc = 0;                                                              \
		do {                                                                     \
			di_type rtype;                                                   \
			di_value ret;                                              \
			bool called;                                                     \
			rc = di_callx((di_object *)(o), di_string_borrow(name),   \
			              &rtype, &ret, di_tuple(__VA_ARGS__), &called);     \
			if (rc != 0) {                                                   \
				break;                                                   \
			}                                                                \
			di_free_value(rtype, &ret);                                      \
		} while (0);                                                             \
		rc;                                                                      \
	})

#define di_callr(o, name, r, ...)                                                        \
	({                                                                               \
		int __deai_callr_rc = 0;                                                 \
		do {                                                                     \
			di_type __deai_callr_rtype;                                      \
			di_value __deai_callr_ret;                                 \
			bool called;                                                     \
			__deai_callr_rc =                                                \
			    di_callx((di_object *)(o), di_string_borrow(name),    \
			             &__deai_callr_rtype, &__deai_callr_ret,             \
			             di_tuple(__VA_ARGS__), &called);                    \
			if (__deai_callr_rc != 0) {                                      \
				break;                                                   \
			}                                                                \
			if (di_typeof(r) != __deai_callr_rtype) {                        \
				di_free_value(__deai_callr_rtype, &__deai_callr_ret);    \
				__deai_callr_rc = -EINVAL;                               \
				break;                                                   \
			}                                                                \
			(r) = *(typeof(r) *)&__deai_callr_ret;                           \
		} while (0);                                                             \
		__deai_callr_rc;                                                         \
	})

#define di_variant(x)                                                                    \
	((struct di_variant){                                                            \
	    (di_value *)addressof(x),                                              \
	    di_typeof(x),                                                                \
	})
#define di_tuple(...)                                                                    \
	((di_tuple){                                                              \
	    VA_ARGS_LENGTH(__VA_ARGS__),                                                 \
	    (struct di_variant[]){LIST_APPLY(di_variant, SEP_COMMA, __VA_ARGS__)}})

#define di_call_callable(c, ...)                                                         \
	({                                                                               \
		int rc = 0;                                                              \
		do {                                                                     \
			di_type rt;                                                      \
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
			di_type rt;                                                      \
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

#define di_has_member(o, name)                                                           \
	(di_lookup((di_object *)(o), di_string_borrow(name)) != NULL)
#define di_emit(o, name, ...)                                                            \
	di_emitn((di_object *)o, di_string_borrow(name), di_tuple(__VA_ARGS__))

/// Register a field of struct `o` as a read only member of the di_object, by using a
/// field getter
#define di_field(o, name)                                                                    \
	({                                                                                   \
		__auto_type __deai_tmp_field_getter =                                        \
		    di_new_field_getter(di_typeof((o)->name), offsetof(typeof(*(o)), name)); \
		di_member((di_object *)(o), "__get_" #name, __deai_tmp_field_getter); \
	})

#define di_member(o, name, v)                                                            \
	di_add_member_move((di_object *)(o), di_string_borrow(name),              \
	                   (di_type[]){di_typeof(v)}, &(v))

#define di_member_clone(o, name, v)                                                      \
	di_add_member_clonev((di_object *)(o), di_string_borrow(name),            \
	                     di_typeof(v), (v))

#define di_getter(o, name, g) di_method(o, STRINGIFY(__get_##name), g)
#define di_setter(o, name, s, type) di_method(o, STRINGIFY(__set_##name), s, type);
#define di_signal_setter_deleter(o, sig, setter, deleter)                                \
	do {                                                                             \
		di_method(o, di_signal_setter_of(sig), setter, di_object *);      \
		di_method(o, di_signal_deleter_of(sig), deleter);                        \
	} while (0)

#define di_signal_setter_deleter_with_signal_name(o, sig, setter, deleter)               \
	do {                                                                             \
		const char *signal_name = di_signal_member_of(sig);                      \
		di_object *setter_closure = (void *)di_closure(                   \
		    setter, (signal_name), di_object *, di_object *);      \
		di_member(o, di_signal_setter_of(sig), setter_closure);                  \
		di_object *deleter_closure =                                      \
		    (void *)di_closure(deleter, (signal_name), di_object *);      \
		di_member(o, di_signal_deleter_of(sig), deleter_closure);                \
	} while (0)

#define di_getter_setter(o, name, g, s)                                                      \
	({                                                                                   \
		int rc = 0;                                                                  \
		do {                                                                         \
			rc = di_getter(o, name, g);                                          \
			if (rc != 0) {                                                       \
				break;                                                       \
			}                                                                    \
			rc = di_setter(o, name, s, di_return_typeof(g, di_object *)); \
		} while (0);                                                                 \
		rc;                                                                          \
	})

// Note: this is just used to create a value of `type`, but this value is not guaranteed
// to be a valid value for `type` at runtime. This is only meant to be used for compile
// time metaprogramming
#define TYPE_INIT(type)                                                                  \
	_Generic((type *)0, \
	di_array *: DI_ARRAY_INIT, \
	di_tuple *: DI_TUPLE_INIT, \
	di_variant *: DI_VARIANT_INIT, \
	int *: 0, \
	unsigned int *: 0, \
	int64_t *: 0, \
	uint64_t *: 0, \
	di_string *: DI_STRING_INIT, \
	const char **: NULL, \
	di_object **: NULL, \
	di_weak_object **: NULL, \
	void **: NULL, \
	double *: 0.0, \
	bool *: false \
	)

#define gen_args(...) LIST_APPLY(TYPE_INIT, SEP_COMMA, ##__VA_ARGS__)

#define di_return_typeof(fn, ...) typeof(fn(gen_args(__VA_ARGS__)))
#define di_return_typeid(fn, ...) di_typeid(di_return_typeof(fn, ##__VA_ARGS__))

#define di_register_typed_method(o, name, fn, rtype, ...)                                \
	di_add_method((di_object *)(o), di_string_borrow(name), (void *)(fn),     \
	              (rtype), VA_ARGS_LENGTH(__VA_ARGS__), ##__VA_ARGS__)

#define INDIRECT(fn, ...) fn(__VA_ARGS__)

// Need to use INDIRECT because macro(A B) is consider to have only one argument,
// even if B expands to something starts with a comma
#define di_method(obj, name, fn, ...)                                                    \
	INDIRECT(di_register_typed_method, obj, name, fn,                                \
	         di_return_typeid(fn, di_object *, ##__VA_ARGS__)                 \
	             LIST_APPLY_pre(di_typeid, SEP_COMMA, ##__VA_ARGS__))

define_object_cleanup(di_closure);
define_object_cleanup(di_promise);

#define scoped_di_closure scopedp(di_closure)
#define scoped_di_promise scopedp(di_promise)

static inline unused const char *nonnull di_type_to_string(di_type type) {
#define TYPE_CASE(name)                                                                  \
	case DI_TYPE_##name:                                                             \
		return #name
	switch (type) {
		LIST_APPLY(TYPE_CASE, SEP_COLON, NIL, ANY, BOOL, INT, UINT, NINT, NUINT,
		           FLOAT, STRING, STRING_LITERAL);
		LIST_APPLY(TYPE_CASE, SEP_COLON, TUPLE, ARRAY, VARIANT, OBJECT,
		           WEAK_OBJECT, POINTER, EMPTY_OBJECT);
	case DI_LAST_TYPE:
		return "LAST_TYPE";
	}
	unreachable();
}

static inline unused char *nonnull di_value_to_string(di_type type, di_value *nonnull value) {
	char *buf = NULL;
	switch (type) {
	case DI_TYPE_OBJECT:
	case DI_TYPE_EMPTY_OBJECT:
		asprintf(&buf, "%s: %p", di_get_type(value->object), value->object);
		break;
	case DI_TYPE_INT:
		asprintf(&buf, "%ld", value->int_);
		break;
	case DI_TYPE_UINT:
		asprintf(&buf, "%lu", value->uint);
		break;
	case DI_TYPE_NINT:
		asprintf(&buf, "%d", value->nint);
		break;
	case DI_TYPE_NUINT:
		asprintf(&buf, "%u", value->nuint);
		break;
	case DI_TYPE_FLOAT:
		asprintf(&buf, "%lf", value->float_);
		break;
	case DI_TYPE_BOOL:
		if (value->bool_) {
			buf = strdup("true");
		} else {
			buf = strdup("false");
		}
		break;
	case DI_TYPE_STRING:
		buf = strndup(value->string.data, value->string.length);
		break;
	case DI_TYPE_STRING_LITERAL:
		buf = strdup(value->string_literal);
		break;
	case DI_TYPE_POINTER:
		asprintf(&buf, "%p", value->pointer);
		break;
	case DI_TYPE_WEAK_OBJECT:
		asprintf(&buf, "%p", value->weak_object);
		break;
	case DI_TYPE_ARRAY:
		asprintf(&buf, "[%s; %ld]", di_type_to_string(value->array.elem_type),
		         value->array.length);
		break;
	case DI_TYPE_VARIANT:;
		char *inner = di_value_to_string(value->variant.type, value->variant.value);
		asprintf(&buf, "variant(%s)", inner);
		free(inner);
		break;
	case DI_TYPE_TUPLE:
	case DI_TYPE_ANY:
	case DI_TYPE_NIL:
	case DI_LAST_TYPE:
	default:
		buf = strdup("");
	}
	return buf;
}

#define DEAI_MEMBER_NAME_RAW "__deai"
#define DEAI_MEMBER_NAME                                                                 \
	((di_string){.data = DEAI_MEMBER_NAME_RAW, .length = strlen(DEAI_MEMBER_NAME_RAW)})

static inline di_object *nullable unused di_object_get_deai_weak(di_object *nonnull o) {
	scoped_di_weak_object *weak = NULL;
	di_get(o, DEAI_MEMBER_NAME_RAW, weak);

	if (weak == NULL) {
		return NULL;
	}
	return di_upgrade_weak_ref(weak);
}

static inline di_object *nullable unused di_object_get_deai_strong(di_object *nonnull o) {
	di_object *strong = NULL;
	di_get(o, DEAI_MEMBER_NAME_RAW, strong);
	return strong;
}

/// Downgrade the __deai member from a strong reference to a weak reference
static inline void unused di_object_downgrade_deai(di_object *nonnull o) {
	scoped_di_object *di_obj = NULL;
	di_get(o, DEAI_MEMBER_NAME_RAW, di_obj);
	if (di_obj != NULL) {
		__auto_type weak = di_weakly_ref_object(di_obj);
		di_remove_member_raw(o, DEAI_MEMBER_NAME);
		di_member(o, DEAI_MEMBER_NAME_RAW, weak);
	}
}
/// Upgrade the __deai member from a weak reference to a strong reference
static inline void unused di_object_upgrade_deai(di_object *nonnull o) {
	scoped_di_weak_object *di_obj = NULL;
	di_get(o, DEAI_MEMBER_NAME_RAW, di_obj);
	if (di_obj != NULL) {
		__auto_type strong = di_upgrade_weak_ref(di_obj);
		di_remove_member_raw(o, DEAI_MEMBER_NAME);
		if (strong != NULL) {
			di_member(o, DEAI_MEMBER_NAME_RAW, strong);
		}
	}
}

static inline di_object *nullable unused di_module_get_deai(struct di_module *nonnull o) {
	return di_object_get_deai_weak((di_object *)o);
}

/// Consumes a di_value and create a di_variant containing that di_value
static inline struct di_variant unused di_variant_of_impl(di_type type,
                                                          di_value *nullable val) {
	struct di_variant ret = {
	    .type = type,
	    .value = NULL,
	};
	if (type == DI_TYPE_NIL || type == DI_LAST_TYPE) {
		return ret;
	}

	ret.value = malloc(di_sizeof_type(type));
	memcpy(ret.value, val, di_sizeof_type(type));
	return ret;
}

/// Get the `prop` member of object `o`. If this member is a weak reference, try upgrading
/// it. If upgrading fails, remove `prop` from `o` and return NULL, otherwise return the
/// upgraded reference,  If the member is not a weak reference, return NULL.
///
/// Getting and removing and done without going through getter or deleter.
static inline di_object *nullable unused di_get_object_via_weak(di_object *nonnull o,
                                                                       di_string prop) {
	scoped_di_weak_object *weak = NULL;
	di_object *object = NULL;
	if (di_rawgetxt(o, prop, DI_TYPE_WEAK_OBJECT, (void *)&weak) == 0) {
		object = di_upgrade_weak_ref(weak);
		if (object == NULL) {
			di_remove_member_raw(o, prop);
		}
	}
	return object;
}

static inline di_value as_di_value(di_type type, void *nonnull value) {
	di_value ret;
	memcpy(&ret, value, di_sizeof_type(type));
	return ret;
}

/// Get the array element at a given index, as a variant. This variant doesn't have
/// ownership, and can't escaped
#define di_array_index(arr, index)                                                       \
	((struct di_variant){                                                            \
	    .type = arr.elem_type,                                                       \
	    .value = (di_value[]){as_di_value(                                     \
	        arr.elem_type, arr.arr + di_sizeof_type(arr.elem_type) + index)}})

#define di_variant_of(v)                                                                 \
	({                                                                               \
		auto __deai_variant_tmp = (v);                                           \
		di_variant_of_impl(di_typeof(__deai_variant_tmp),                        \
		                   (di_value *)&__deai_variant_tmp);               \
	})

/// Variant of the bottom type. Meaning this variant "doesn't exist", used to indicate
/// a property doesn't exist by generic getters.
static const struct di_variant DI_BOTTOM_VARIANT = (struct di_variant){NULL, DI_LAST_TYPE};

/// Shortcut for calling di_remove_member_raw then di_add_member_clone
static inline unused int di_rawsetx(di_object *nonnull o, di_string prop,
                                    di_type type, const void *nonnull value) {
	int ret = di_remove_member_raw(o, prop);
	if (ret != 0 && ret != -ENOENT) {
		return ret;
	}
	return di_add_member_clone(o, prop, type, value);
}
