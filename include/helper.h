/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

#include <deai.h>
#include <string.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

int di_setv(struct di_object *o, const char *prop, di_type_t type, void *val);
int di_getv(struct di_object *o, const char *prop, di_type_t *type, void **val);
int di_register_field_getter(struct di_object *o, const char *prop, off_t offset,
                             di_type_t t);

static inline int integer_conversion(di_type_t inty, const void *inp,
                                     di_type_t outty, const void **outp) {
	if (inty == outty) {
		*outp = inp;
		return 0;
	}

#define convert_case(srct, dstt, dstmax, dstmin)                                    \
	case di_typeof((srct)0):                                                    \
		do {                                                                \
			srct tmp = *(srct *)(inp);                                  \
			if (tmp > (dstmax) || tmp < (dstmin))                       \
				return -ERANGE;                                     \
			dstt *tmp2 = malloc(sizeof(dstt));                          \
			*tmp2 = tmp;                                                \
			*outp = tmp2;                                               \
		} while (0);                                                        \
		break

#define convert_switch(s1, s2, s3, ...)                                             \
	switch (inty) {                                                             \
		convert_case(s1, __VA_ARGS__);                                      \
		convert_case(s2, __VA_ARGS__);                                      \
		convert_case(s3, __VA_ARGS__);                                      \
	default: return -EINVAL;                                                    \
	}

#define RET_IF_ERR(expr)                                                            \
	do {                                                                        \
		int ret = (expr);                                                   \
		if (ret != 0)                                                       \
			return ret;                                                 \
	} while (0)

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
	default: return -EINVAL;
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

static inline int number_conversion(di_type_t inty, const void *inp, di_type_t outty,
                                    const void **outp) {
	if (inty == outty) {
		*outp = inp;
		return 0;
	}

	if (is_integer(inty)) {
		if (is_integer(outty))
			return integer_conversion(inty, inp, outty, outp);
		if (outty == DI_TYPE_FLOAT) {
			double *res = malloc(sizeof(double));
#define convert_case(srct)                                                          \
	case di_typeof((srct)0): *res = *(srct *)inp; return 0;
			switch (inty) {
				convert_case(unsigned int);
				convert_case(int);
				convert_case(uint64_t);
				convert_case(int64_t);
			default: return -EINVAL;
			}
#undef convert_case
			*outp = res;
			return 0;
		}
	}

	// float -> integer not allowed
	return -EINVAL;
}

#define di_set(o, prop, v)                                                          \
	({                                                                          \
		__auto_type __tmp = (v);                                            \
		di_setv(o, prop, di_typeof(__tmp), &__tmp);                         \
	})

#define di_get(o, prop, r)                                                          \
	({                                                                          \
		void *ret;                                                          \
		const void *ret2;                                                   \
		di_type_t rtype;                                                    \
		int rc;                                                             \
		do {                                                                \
			rc = di_getv(o, prop, &rtype, &ret);                        \
			if (rc != 0)                                                \
				break;                                              \
			rc = number_conversion(rtype, ret, di_typeof(r), &ret2);    \
			if (rc)                                                     \
				break;                                              \
			(r) = *(typeof(r) *)ret2;                                   \
			free(ret);                                                  \
			if (ret2 != ret)                                            \
				free((void *)ret2);                                 \
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

#define VA_ARGS_LENGTH_(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, N, ...) N
#define VA_ARGS_LENGTH(...)                                                         \
	VA_ARGS_LENGTH_(0, ##__VA_ARGS__, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)

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
#define LIST_APPLY(fn, sep, ...) LIST_APPLY_(VA_ARGS_LENGTH(__VA_ARGS__), fn, sep, __VA_ARGS__)

#define SEP_COMMA() ,
#define SEP_COLON() ;
#define SEP_NONE()

#define STRINGIFY(x) #x

#define di_type_pair(v) di_typeof(v), v,
#define di_arg_list(...) LIST_APPLY(di_type_pair, SEP_NONE, __VA_ARGS__) DI_LAST_TYPE

#define object_cleanup __attribute__((cleanup(di_cleanup_objectp)))

#define di_getm_ex(di, var, modn)                                                   \
	object_cleanup struct di_object *var = (void *)di_find_module((di), modn)
#define di_getm(di, modn) di_getm_ex(di, modn##m, #modn)

// call but ignore return
#define di_call0(o, name, ...)                                                      \
	({                                                                          \
		int rc = 0;                                                         \
		do {                                                                \
			__auto_type c =                                             \
			    di_find_method((struct di_object *)(o), (name));        \
			if (!c) {                                                   \
				rc = -ENOENT;                                       \
				break;                                              \
			}                                                           \
			di_type_t rtype;                                            \
			void *ret;                                                  \
			rc = di_call_callable_v((void *)c, &rtype, &ret,            \
			                        di_arg_list(__VA_ARGS__));          \
			if (rc != 0)                                                \
				break;                                              \
			di_free_value(rtype, ret);                                  \
		} while (0);                                                        \
		rc;                                                                 \
	})

#define di_call(o, name, r, ...)                                                    \
	({                                                                          \
		int rc = 0;                                                         \
		do {                                                                \
			__auto_type c =                                             \
			    di_find_method((struct di_object *)(o), (name));        \
			if (!c) {                                                   \
				rc = -ENOENT;                                       \
				break;                                              \
			}                                                           \
			di_type_t rtype;                                            \
			void *ret;                                                  \
			rc = di_call_callable_v((void *)c, &rtype, &ret,            \
			                        di_arg_list(__VA_ARGS__));          \
			if (rc != 0)                                                \
				break;                                              \
			if (di_typeof(r) != rtype) {                                \
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

#define di_dtor(o, dtor)                                                            \
	di_register_typed_method((struct di_object *)o, (di_fn_t)dtor, "__dtor",    \
	                         DI_TYPE_VOID, 0);

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

#define di_method(obj, name, fn, ...)                                               \
	di_register_typed_method(                                                   \
	    (struct di_object *)obj, (di_fn_t)fn, name,                             \
	    di_return_typeof(fn, struct di_object *, __VA_ARGS__),                  \
	    VA_ARGS_LENGTH(__VA_ARGS__), LIST_APPLY(di_typeid, SEP_COMMA, __VA_ARGS__))

static inline void free_charp(char **ptr) {
	free(*ptr);
	*ptr = NULL;
}

static inline int
di_register_rw_property(struct di_object *obj, const char *name, di_fn_t prop_r,
                        di_fn_t prop_w, di_type_t t) {
	__attribute__((cleanup(free_charp))) char *buf =
	    malloc(strlen(name) + strlen("__get_") + 1);
	if (!buf)
		return -ENOMEM;

	strcpy(buf, "__get_");
	strcat(buf, name);

	RET_IF_ERR(di_register_typed_method(obj, prop_r, buf, t, 0));

	if (prop_w) {
		strcpy(buf, "__set_");
		strcat(buf, name);
		RET_IF_ERR(
		    di_register_typed_method(obj, prop_w, buf, DI_TYPE_VOID, 1, t));
	}
	return 0;
}

#define di_rprop(o, name, prop_r)                                                   \
	di_register_rw_property((void *)o, name, (di_fn_t)prop_r, NULL,             \
	                        di_typeof((prop_r)(NULL)))
#define di_rwprop(o, name, prop_r, prop_w)                                          \
	di_register_rw_property((void *)o, name, (di_fn_t)prop_r, (di_fn_t)prop_w,  \
	                        di_typeof((prop_r)(NULL)))

// TODO maybe
// macro to generate c wrapper for di functions
