/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

#include <deai/common.h>
#include <deai/compiler.h>

#include <assert.h>
#include <errno.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// deai type ids. Use negative numbers for invalid types.
///
/// # Passing by reference:
///
/// * Arrays are passed by value, which contains a pointer to the array storage. It has the
///   same effect as C++ vectors. You can modify the elements of the array, but if you
///   change the storage pointer, it won't be reflected in the actual array. The same
///   applies to tuples as well. Also, the language plugins will always copy the array or
///   tuple and convert it to a language native value, so modifications made by scripts to
///   an array or tuple will never be reflected.
/// * Although it's possible to pass a value by reference by wrapping it in a variant, it
///   is discouraged. For convenience, the language plugins will always "unpack" the
///   variant and make a copy of its inner value. If you want to pass something the
///   scripts can modify, you should always use an object.
///
/// In summary, di_objects are only passed by references, all basic types are always
/// passed by value. strings, arrays, tuples and variants are in the middle. If you use the
/// deai API directly, they pass their inner values as references; if you are writing a
/// script, they would be unpacked recursively, and their inner values will be passed by
/// value to your script.
///
/// ## Examples:
///
/// ```c
/// int fun(struct di_array arr) {
///     ((int *)arr.arr)[0] = 1; // reflected
///     arr.arr = realloc(arr.arr, 20); // not reflected
///     ((int *)arr.arr)[0] = 1; // not reflected
/// }
/// ```
///
/// ```lua
/// function(array)
///     array[0] = 1 -- not reflected
///     table.insert(array, 10) -- not reflected
/// end
/// ```
///
/// # NULL object refernce
///
/// deai object references MUST NOT be NULL. However, it is sometimes useful to have a
/// value that is either an object reference or nil. Use di_variant in that case.
///
/// If you are writing a specialized getter, you should reconsider returning nil from the
/// getter. Because many script languages treat nil to mean the property doesn't exist.
/// But having a specialized getter defined for a property, already indicates that the
/// property does exist. This can confuse the user. Instead, return an empty object, or an
/// error object in case there was an error getting the property.
///
/// If you want to have a property that may or may not exist, you should write a generic
/// getter, and return a variant whose type is DI_LAST_TYPE to indicate the property
/// doesn't exist.
typedef enum di_type {
	DI_TYPE_NIL = 0,
	// unresolved, only used for element type of empty arrays.
	DI_TYPE_ANY,
	// boolean, no implicit conversion to number types
	// C type: _Bool
	DI_TYPE_BOOL,
	// native integer
	// C type: int
	DI_TYPE_NINT,
	// native unsigned integer
	// C type: unsigned int
	DI_TYPE_NUINT,
	// 64bit signed integer, int64_t
	// C type: int64_t
	DI_TYPE_INT,
	// 64bit unsigned integer
	// C type: uint64_t
	DI_TYPE_UINT,
	// implementation defined floating point number type
	// C type: double
	DI_TYPE_FLOAT,
	// generic pointer, whose memory is not managed by deai
	// C type: void *
	DI_TYPE_POINTER,
	// a deai object reference
	// C type: struct di_object *
	DI_TYPE_OBJECT,
	// a weak deai object reference
	// C type: struct di_weak_object
	DI_TYPE_WEAK_OBJECT,
	// utf-8 string
	// C type: char *
	DI_TYPE_STRING,
	// utf-8 string literal. can also mean a string whose memory is not managed by
	// deai.
	// C type: const char *
	DI_TYPE_STRING_LITERAL,
	// an array. all elements in the array have the same type. see `struct di_array`
	// for more info.
	// C type: struct di_array
	DI_TYPE_ARRAY,
	// a tuple. a collection of variable number of elements, each with its own type.
	// C type: struct di_tuple
	DI_TYPE_TUPLE,
	// sum type of all deai types. note: variants always hold a reference to their
	// inner values. passing a variant is equivalent to passing a reference to the
	// value.
	// C type: struct di_variant
	DI_TYPE_VARIANT,
	// the bottom type
	// this type has no value. it shouldn't be used as type for parameters or
	// arguments. and shouldn't be used as return type, except for a getter.
	// the getters are allowed to return a variant of LAST_TYPE, to indicate the
	// property is not found.
	DI_LAST_TYPE,
} di_type_t;

struct di_object;
struct di_tuple;
union di_value;
typedef int (*di_call_fn_t)(struct di_object *nonnull, di_type_t *nonnull rt,
                            union di_value *nonnull ret, struct di_tuple);
typedef void (*di_dtor_fn_t)(struct di_object *nonnull);
struct di_signal;
struct di_listener;
struct di_callable;
struct di_member;
struct di_module;
struct di_weak_object;

struct di_object {
	alignas(8) char padding[128];
};

struct di_array {
	uint64_t length;
	// `arr` is an array of `type`. e.g. if `elem_type` is DI_TYPE_INT, then
	// `arr` points to a `uint64_t[]`.
	void *nullable arr;        // null if length == 0
	di_type_t elem_type;
};

struct di_tuple {
	uint64_t length;
	struct di_variant *nullable elements;        // null if length == 0
};

struct di_variant {
	union di_value *nullable value;        // null in case of nil
	di_type_t type;
};

/// All builtin deai types
union di_value {
	// void unit;
	// ? any;
	bool bool_;
	int nint;
	unsigned int nuint;
	int64_t int_;
	uint64_t uint;
	double float_;
	void *nullable pointer;
	struct di_object *nonnull object;
	struct di_weak_object *nonnull weak_object;
	char *nonnull string;
	const char *nonnull string_literal;
	struct di_array array;
	struct di_tuple tuple;
	struct di_variant variant;
	// ! last_type
};

/// Fetch member object `name` from object `o`, then call the member object with `args`.
///
/// # Errors
///
/// * EINVAL: if the member object is not callable.
/// * ENOENT: if the member object doesn't exist.
///
/// @param[out] rt The return type of the function
/// @param[out] ret The return value, MUST BE a pointer to a full di_value
PUBLIC_DEAI_API int
di_rawcallxn(struct di_object *nonnull o, const char *nonnull name, di_type_t *nonnull rt,
             union di_value *nonnull ret, struct di_tuple args, bool *nonnull called);

/// Like `di_rawcallxn`, but also calls getter functions to fetch the member object. And
/// the arguments are pass as variadic arguments. Arguments are passed as pairs of type
/// ids and values, end with DI_LAST_TYPE.
///
/// You shouldn't use this function directly, use the `di_call` macro if you are using C.
PUBLIC_DEAI_API int
di_callx(struct di_object *nonnull o, const char *nonnull name, di_type_t *nonnull rt,
         union di_value *nonnull ret, struct di_tuple args, bool *nonnull called);

/// Change the value of member `prop` of object `o`.
///
/// If a specialized setter `__set_<prop>` exists, it will call the setter. If not, it
/// will try the generic setter, `__set`. Then, it will try to change the value of
/// `prop` if the exists. At last, it will add a new member to the object
///
/// @param[in] type The type of the value
/// @param[in] val The value, borrowed.
PUBLIC_DEAI_API int di_setx(struct di_object *nonnull o, const char *nonnull prop,
                            di_type_t type, const void *nullable val);

/// Fetch a member with name `prop` from an object `o`, without calling the getter
/// functions. The value is cloned, then returned.
///
/// # Errors
///
/// * ENOENT: member `prop` not found.
///
/// @param[out] type Type of the value
/// @param[out] ret The value, MUST BE a pointer to a full `union di_value`
/// @return 0 for success, or an error code.
PUBLIC_DEAI_API int di_rawgetx(struct di_object *nonnull o, const char *nonnull prop,
                               di_type_t *nonnull type, union di_value *nonnull ret);

/// Like `di_rawgetx`, but tries to do automatic type conversion to the desired type `type`.
///
/// # Errors
///
/// Same as `di_rawgetx`, plus:
///
/// * EINVAL: if type conversion failes.
///
/// @param[out] ret The value
/// @return 0 for success, or an error code.
PUBLIC_DEAI_API int di_rawgetxt(struct di_object *nonnull o, const char *nonnull prop,
                                di_type_t type, union di_value *nonnull ret);

/// Like `di_rawgetx`, but also calls getter functions if `prop` is not found.
/// The getter functions are the generic getter "__get", or the specialized getter
/// "__get_<prop>". The specialized getter is called if it exists, if not, the generic
/// getter is called.
///
/// The getter can return a normal value, or a variant. Variants returned by the getters
/// are automatically unpacked recursively. A variant of DI_LAST_TYPE can be used by the
/// generic getter ("__get") to indicate that `prop` doesn't exist in `o`. Specifialized
/// getter cannot return DI_LAST_TYPE.
PUBLIC_DEAI_API int di_getx(struct di_object *nonnull o, const char *nonnull prop,
                            di_type_t *nonnull type, union di_value *nonnull ret);

/// Like `di_rawgetxt`, but also calls getter functions if `prop` is not found.
PUBLIC_DEAI_API int di_getxt(struct di_object *nonnull o, const char *nonnull prop,
                             di_type_t type, union di_value *nonnull ret);

/// Set the "__type" member of the object `o`. By convention, "__type" names the type of
/// the object. Type names should be formated as "<namespace>:<type>". The "deai"
/// namespace is used by deai.
///
/// @param[in] type The type name, must be a string literal
PUBLIC_DEAI_API int di_set_type(struct di_object *nonnull o, const char *nonnull type);

/// Get the type name of the object
///
/// @return A const string, the type name. It shouldn't be freed.
PUBLIC_DEAI_API const char *nonnull di_get_type(struct di_object *nonnull o);

/// Check if the type of the object is `type`
PUBLIC_DEAI_API bool di_check_type(struct di_object *nonnull o, const char *nonnull type);

/// Add value (*address) with type `*type` as a member named `name` of object `o`. This
/// function takes the ownership of *address. After this call, `*type` and `*address` will
/// be set to invalid values.
PUBLIC_DEAI_API int nonnull_all di_add_member_move(struct di_object *nonnull o,
                                                   const char *nonnull name,
                                                   di_type_t *nonnull type,
                                                   void *nonnull address);

/// Add a value with type `type` as a member named `name` of object `o`. This function
/// will cloned the value before adding it as a member.
PUBLIC_DEAI_API int nonnull_all di_add_member_clone(struct di_object *nonnull o,
                                                    const char *nonnull name, di_type_t, ...);

/// Remove a member of object `o`, without calling the deleter.
PUBLIC_DEAI_API int di_remove_member_raw(struct di_object *nonnull o, const char *nonnull name);
/// Remove a member of object `o`, or call its deleter.
/// If the specialized deleter `__delete_<name>` exists, it will be called; if not,
/// the generic deleter, `__delete`, will be tried. At last, this function will try to
/// find and remove a member with name `name`.
///
/// `name` cannot name an internal member
PUBLIC_DEAI_API int di_remove_member(struct di_object *nonnull o, const char *nonnull name);

/// Check whether a member with `name` exists in the object, without calling the
/// getters. Returns non-NULL if the member exists, and NULL otherwise.
///
/// This function doesn't retreive the member, no reference counter is incremented.
PUBLIC_DEAI_API struct di_member *nullable di_lookup(struct di_object *nonnull,
                                                     const char *nonnull name);
PUBLIC_DEAI_API struct di_object *nullable di_new_object(size_t sz, size_t alignment);

/// Listen to signal `name` emitted from object `o`. When the signal is emitted, handler
/// `h` will be called. If the returned object is dropped, the listen-to relationship is
/// automatically stopped.
///
/// Return object type: ListenerHandle
PUBLIC_DEAI_API struct di_object *nullable di_listen_to(struct di_object *nonnull,
                                                        const char *nonnull name,
                                                        struct di_object *nullable h);

/// Emit a signal with `name`, and `args`. The emitter of the signal is responsible of
/// freeing `args`.
PUBLIC_DEAI_API int
di_emitn(struct di_object *nonnull, const char *nonnull name, struct di_tuple args);
// Call object dtor, remove all listeners and members from the object. And free the
// memory
// if the ref count drop to 0 after this process
PUBLIC_DEAI_API void di_destroy_object(struct di_object *nonnull);

PUBLIC_DEAI_API struct di_object *
    nonnull allocates(malloc) di_ref_object(struct di_object *nonnull);

/// Create a weak reference to a di_object. The object could be NULL, in that case, an
/// empty reference is created.
PUBLIC_DEAI_API struct di_weak_object *nonnull di_weakly_ref_object(struct di_object *nullable);

/// Upgrade a weak object reference to an object reference.
///
/// @return An object reference, or NULL if the object has been freed.
PUBLIC_DEAI_API struct di_object *
    nullable allocates(malloc) di_upgrade_weak_ref(struct di_weak_object *nonnull);

/// Drop a weak object reference. After this function returns, the passed pointer will
/// become invalid
PUBLIC_DEAI_API void di_drop_weak_ref(struct di_weak_object *nonnull *nonnull);
PUBLIC_DEAI_API void frees(malloc, 1) di_unref_object(struct di_object *nonnull);

PUBLIC_DEAI_API void di_set_object_dtor(struct di_object *nonnull, di_dtor_fn_t nullable);
PUBLIC_DEAI_API void di_set_object_call(struct di_object *nonnull, di_call_fn_t nullable);
PUBLIC_DEAI_API bool di_is_object_callable(struct di_object *nonnull);

PUBLIC_DEAI_API void di_free_tuple(struct di_tuple);
PUBLIC_DEAI_API void di_free_array(struct di_array);

/// Free a `value` of type `t`. This function does not free the storage space used by
/// `value`. This is to make this function usable for values stored on the stack.
PUBLIC_DEAI_API void di_free_value(di_type_t t, union di_value *nullable value_ptr);

/// Copy value of type `t` from `src` to `dst`. It's assumed that `dst` has enough memory
/// space to hold a value of type `t`, and that `dst` doesn't contain a valid value
/// beforehand
PUBLIC_DEAI_API void di_copy_value(di_type_t t, void *nullable dst, const void *nullable src);

static inline unused size_t di_sizeof_type(di_type_t t) {
	switch (t) {
	case DI_TYPE_NIL:
		return 0;
	case DI_TYPE_ANY:
	case DI_LAST_TYPE:
		abort();
	case DI_TYPE_FLOAT:
		return sizeof(double);
	case DI_TYPE_ARRAY:
		return sizeof(struct di_array);
	case DI_TYPE_TUPLE:
		return sizeof(struct di_tuple);
	case DI_TYPE_VARIANT:
		return sizeof(struct di_variant);
	case DI_TYPE_UINT:
	case DI_TYPE_INT:
		return sizeof(int64_t);
	case DI_TYPE_NUINT:
		return sizeof(unsigned int);
	case DI_TYPE_NINT:
		return sizeof(int);
	case DI_TYPE_STRING:
	case DI_TYPE_STRING_LITERAL:
	case DI_TYPE_OBJECT:
	case DI_TYPE_POINTER:
		return sizeof(void *);
	case DI_TYPE_WEAK_OBJECT:
		return sizeof(struct di_weak_object *);
	case DI_TYPE_BOOL:
		return sizeof(bool);
	}
	abort();
}

// Workaround for _Generic limitations, see:
// http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1930.htm
#define di_typeid(x)                                                                     \
	_Generic((x*)0, \
	struct di_array *: DI_TYPE_ARRAY, \
	struct di_tuple *: DI_TYPE_TUPLE, \
	struct di_variant *: DI_TYPE_VARIANT, \
	int *: DI_TYPE_NINT, \
	unsigned int *: DI_TYPE_NUINT, \
	int64_t *: DI_TYPE_INT, \
	uint64_t *: DI_TYPE_UINT, \
	char **: DI_TYPE_STRING, \
	const char **: DI_TYPE_STRING_LITERAL, \
	struct di_object **: DI_TYPE_OBJECT, \
	struct di_weak_object **: DI_TYPE_WEAK_OBJECT, \
	void **: DI_TYPE_POINTER, \
	double *: DI_TYPE_FLOAT, \
	void *: DI_TYPE_NIL, \
	bool *: DI_TYPE_BOOL \
)

#define di_typeof(expr) di_typeid(typeof(expr))

#define di_set_return(v)                                                                 \
	do {                                                                             \
		*rtype = di_typeof(v);                                                   \
		typeof(v) *retv;                                                         \
		if (!*ret)                                                               \
			*ret = calloc(1, di_min_return_size(sizeof(v)));                 \
		retv = *(typeof(v) **)ret;                                               \
		*retv = v;                                                               \
	} while (0);

/// A constant to create an empty array
static const struct di_array unused DI_ARRAY_INIT = {0, NULL, DI_TYPE_ANY};
/// A constant to create an empty tuple
static const struct di_tuple unused DI_TUPLE_INIT = {0, NULL};
/// A constant to create an nil variant
static const struct di_variant unused DI_VARIANT_INIT = {NULL, DI_TYPE_NIL};

#define define_object_cleanup(object_type)                                                    \
	static inline void unused di_free_##object_type##p(                              \
	    struct object_type *nullable *nonnull p) {                                   \
		if (*p) {                                                                \
			di_unref_object((struct di_object *)*p);                         \
		}                                                                        \
		*p = NULL;                                                               \
	}
#define with_object_cleanup(t) with_cleanup(di_free_##t##p) struct t *

unused define_object_cleanup(di_object);

/// A valid but non-upgradeable weak reference
PUBLIC_DEAI_API extern const struct di_weak_object *const nonnull dead_weak_ref;
