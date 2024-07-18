/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

#include "common.h"
#include "compiler.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __cplusplus
#define DI_TYPE_NAME(x) DI_TYPE_##x
#else
#define DI_TYPE_NAME(x) x
#define __auto_type auto
#endif

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
/// int fun(di_array arr) {
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
#ifdef __cplusplus
enum class di_type : int {
#else
typedef enum di_type {
#endif
	DI_TYPE_NAME(NIL) = 0,
	// unresolved, only used for element type of empty arrays.
	DI_TYPE_NAME(ANY),
	// an empty object, this is treated as special case because it could be converted
	// to an empty array. emptyness of the object is actually not checked, because
	// it's almost impossible: e.g. an object could have a getter that always return
	// nothing. CANNOT be used as a parameter type.
	DI_TYPE_NAME(EMPTY_OBJECT),
	// boolean, no implicit conversion to number types
	// C type: _Bool
	DI_TYPE_NAME(BOOL),
	// native integer
	// C type: int
	DI_TYPE_NAME(NINT),
	// native unsigned integer
	// C type: unsigned int
	DI_TYPE_NAME(NUINT),
	// 64bit signed integer), int64_t
	// C type: int64_t
	DI_TYPE_NAME(INT),
	// 64bit unsigned integer
	// C type: uint64_t
	DI_TYPE_NAME(UINT),
	// implementation defined floating point number type
	// C type: double
	DI_TYPE_NAME(FLOAT),
	// generic pointer, whose memory is not managed by deai
	// C type: void *
	DI_TYPE_NAME(POINTER),
	// a deai object reference
	// C type: di_object *
	DI_TYPE_NAME(OBJECT),
	// a weak deai object reference
	// C type: struct di_weak_object
	DI_TYPE_NAME(WEAK_OBJECT),
	// a string. a fat pointer to a char array, with length
	// C type: di_string
	DI_TYPE_NAME(STRING),
	// a string, whose memory is not managed by deai.
	// C type: const char *
	DI_TYPE_NAME(STRING_LITERAL),
	// an array. all elements in the array have the same type.
	// a fat pointer with a type and a length.
	// C type: di_array
	DI_TYPE_NAME(ARRAY),
	// a tuple. a collection of variable number of elements, each with its own type.
	// C type: di_tuple
	DI_TYPE_NAME(TUPLE),
	// sum type of all deai types. note: variants always hold a reference to their
	// inner values. passing a variant is equivalent to passing a reference to the
	// value.
	// C type: di_variant
	DI_TYPE_NAME(VARIANT),
	// the bottom type
	// this type has no value. it shouldn't be used as type for parameters or
	// arguments. and shouldn't be used as return type, except for a getter.
	// the getters are allowed to return a variant of LAST_TYPE, to indicate the
	// property is not found.
	DI_LAST_TYPE,
#ifdef __cplusplus
};
using di_type = di_type;
#undef DI_TYPE_NAME
#define DI_TYPE_NAME(x) di_type::x
#define DI_LAST_TYPE di_type::DI_LAST_TYPE
#else
} di_type;
// Make sure C and C++ agrees on the type of di_type
static_assert(sizeof(di_type) == sizeof(int), "di_type has wrong type");

#endif
PUBLIC_DEAI_API extern const char *nonnull di_type_names[];
typedef struct di_object di_object;
typedef struct di_tuple di_tuple;
typedef struct di_variant di_variant;

/// A pending value
///
/// This encapsulates a pending value. Once this value become available, a "resolved"
/// signal will be emitted with the value. Each promise should resolve only once ever.
typedef struct di_promise di_promise;
typedef union di_value di_value;
typedef int (*di_call_fn)(di_object *nonnull, di_type *nonnull rt, di_value *nonnull ret, di_tuple);
typedef void (*di_dtor_fn)(di_object *nonnull);
typedef struct di_signal di_signal_t;
typedef struct di_listener di_listener_t;
typedef struct di_callable di_callable_t;
typedef struct di_member di_member_t;
typedef struct di_module di_module_t;
typedef struct di_weak_object di_weak_object;

struct di_object {
	// NOLINTNEXTLINE(readability-magic-numbers)
	alignas(8) char padding[128];
};

typedef struct di_array {
	uint64_t length;
	// `arr` is an array of `type`. e.g. if `elem_type` is DI_TYPE_INT, then
	// `arr` points to a `uint64_t[]`.
	void *nullable arr;        // null if length == 0
	di_type elem_type;
} di_array;

struct di_tuple {
	uint64_t length;
	di_variant *nullable elements;        // null if length == 0
};

struct di_variant {
	di_value *nullable value;        // null in case of nil
	di_type type;
};

typedef struct di_string {
	const char *nullable data;
	size_t length;
} di_string;

/// A constant to create an empty array
static const di_array unused DI_ARRAY_INIT = {0, NULL, DI_TYPE_NAME(ANY)};
/// A constant to create an empty tuple
static const di_tuple unused DI_TUPLE_INIT = {0, NULL};
/// A constant to create an nil variant
static const di_variant unused DI_VARIANT_INIT = {NULL, DI_TYPE_NAME(NIL)};

static const di_string unused DI_STRING_INIT = {NULL, 0};

/// All builtin deai types
union di_value {
	// void unit;
	// ? any;
	// di_object *empty_object; - stored in `object` instead
	bool bool_;
	int nint;
	unsigned int nuint;
	int64_t int_;
	uint64_t uint;
	double float_;
	void *nullable pointer;
	di_object *nonnull object;
	di_weak_object *nonnull weak_object;
	di_string string;
	const char *nonnull string_literal;
	di_array array;
	di_tuple tuple;
	di_variant variant;
	// ! last_type
};

/// Return the roots registry. ref/unref-ing the roots are not needed
PUBLIC_DEAI_API di_object *nonnull di_get_roots(void);

/// Fetch member object `name` from object `o`, then call the member object with `args`.
/// The member object may be fetched by calling the getter functions.
///
/// You shouldn't use this function directly, use the `di_call` macro if you are using C.
///
/// # Errors
///
/// * EINVAL: if the member object is not callable.
/// * ENOENT: if the member object doesn't exist.
///
/// @param[out] rt The return type of the function
/// @param[out] ret The return value, MUST BE a pointer to a full di_value
PUBLIC_DEAI_API int di_callx(di_object *nonnull o, di_string name, di_type *nonnull rt,
                             di_value *nonnull ret, di_tuple args, bool *nonnull called);

/// Change the value of member `prop` of object `o`.
///
/// If a specialized setter `__set_<prop>` exists, it will call the setter. If not, it
/// will try the generic setter, `__set`. Then, it will try to change the value of
/// `prop` if the exists. At last, it will add a new member to the object
///
/// @param[in] type The type of the value
/// @param[in] val The value, borrowed.
PUBLIC_DEAI_API int
di_setx(di_object *nonnull o, di_string prop, di_type type, const void *nullable val);

/// Fetch reference to a member with name `prop` from an object `o`, without calling the
/// getter functions. The caller can change the value of the member via the reference.
/// Such uate will not involve the setter functions.
///
/// # Errors
///
/// * ENOENT: member `prop` not found.
///
/// @param[out] type Type of the value
/// @param[out] ret Reference to the value.
/// @return 0 for success, or an error code.
PUBLIC_DEAI_API int di_refrawgetx(di_object *nonnull o, di_string prop,
                                  di_type *nonnull type, di_value *nullable *nonnull ret);
/// Fetch a member with name `prop` from an object `o`, without calling the getter
/// functions. The value is cloned, then returned.
///
/// # Errors
///
/// * ENOENT: member `prop` not found.
///
/// @param[out] type Type of the value
/// @param[out] ret The value, MUST BE a pointer to a full `di_value`
/// @return 0 for success, or an error code.
PUBLIC_DEAI_API int di_rawgetx(di_object *nonnull o, di_string prop,
                               di_type *nonnull type, di_value *nonnull ret);

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
PUBLIC_DEAI_API int
di_rawgetxt(di_object *nonnull o, di_string prop, di_type type, di_value *nonnull ret);

/// Like `di_rawgetx`, but also calls getter functions if `prop` is not found.
/// The getter functions are the generic getter "__get", or the specialized getter
/// "__get_<prop>". The specialized getter is called if it exists, if not, the generic
/// getter is called.
///
/// The getter can return a normal value, or a variant. Variants returned by the getters
/// are automatically unpacked recursively. A variant of DI_LAST_TYPE can be used by the
/// generic getter ("__get") to indicate that `prop` doesn't exist in `o`. Specialized
/// getter cannot return DI_LAST_TYPE.
///
/// The returned value holds ownership.
PUBLIC_DEAI_API int
di_getx(di_object *nonnull o, di_string prop, di_type *nonnull type, di_value *nonnull ret);

/// Like `di_rawgetxt`, but also calls getter functions if `prop` is not found.
PUBLIC_DEAI_API int
di_getxt(di_object *nonnull o, di_string prop, di_type type, di_value *nonnull ret);

/// Set the "__type" member of the object `o`. By convention, "__type" names the type of
/// the object. Type names should be formated as "<namespace>:<type>". The "deai"
/// namespace is used by deai.
///
/// @param[in] type The type name, must be a string literal
PUBLIC_DEAI_API int di_set_type(di_object *nonnull o, const char *nonnull type);

/// Get the type name of the object
///
/// @return A const string, the type name. It shouldn't be freed.
PUBLIC_DEAI_API const char *nonnull di_get_type(di_object *nonnull o);

/// Check if the type of the object is `type`
PUBLIC_DEAI_API bool di_check_type(di_object *nonnull o, const char *nonnull type);

/// Add value (*address) with type `*type` as a member named `name` of object `o`. This
/// function takes the ownership of *address. After this call, `*type` and `*address` will
/// be set to invalid values.
PUBLIC_DEAI_API int nonnull_all di_add_member_move(di_object *nonnull o, di_string name,
                                                   di_type *nonnull type, void *nonnull address);

/// Add a value with type `type` as a member named `name` of object `o`. This function
/// will cloned the value before adding it as a member.
PUBLIC_DEAI_API int nonnull_all di_add_member_clone(di_object *nonnull o, di_string name,
                                                    di_type, const void *nonnull value);

/// Add a value with type `type` as a member named `name` of object `o`. This function
/// will clone the value before adding it as a member.
PUBLIC_DEAI_API int nonnull_all di_add_member_clonev(di_object *nonnull o, di_string name,
                                                     di_type, ...);

/// Remove a member of object `o`, without calling the deleter.
PUBLIC_DEAI_API int di_delete_member_raw(di_object *nonnull o, di_string name);

/// Remove a member of object `o`, without calling the deleter. Transfer the ownership of
/// the member to the caller via `ret`.
PUBLIC_DEAI_API int
di_remove_member_raw(di_object *nonnull obj, di_string name, di_variant *nonnull ret);

/// Remove a member of object `o`, or call its deleter.
/// If the specialized deleter `__delete_<name>` exists, it will be called; if not,
/// the generic deleter, `__delete`, will be tried. At last, this function will try to
/// find and remove a member with name `name`.
///
/// `name` cannot name an internal member
PUBLIC_DEAI_API int di_delete_member(di_object *nonnull o, di_string name);

/// Check whether a member with `name` exists in the object, without calling the
/// getters. Returns non-NULL if the member exists, and NULL otherwise.
///
/// This function doesn't retreive the member, no reference counter is incremented.
PUBLIC_DEAI_API struct di_member *nullable di_lookup(di_object *nonnull, di_string name);
PUBLIC_DEAI_API di_object *nullable di_new_object(size_t sz, size_t alignment);
/// Initialized an di_object allocated somewhere else. The allocation must
/// have been 0 initialized. And the memory must be allocated in a way that it can be
/// freed with `free`.
PUBLIC_DEAI_API void di_init_object(di_object *nonnull obj_);

static inline di_object *nullable unused di_new_object_with_type_name(size_t size, size_t alignment,
                                                                      const char *nonnull type) {
	__auto_type ret = di_new_object(size, alignment);
	if (ret == NULL) {
		abort();
	}
	di_set_type(ret, type);
	return ret;
}

/// Listen to signal `name` emitted from object `o`. When the signal is emitted, handler
/// `h` will be called. If the returned object is dropped, the listen-to relationship is
/// automatically stopped.
///
/// Ownership of `h` is not transfered.
///
/// Return object type: ListenerHandle
PUBLIC_DEAI_API di_object *nullable di_listen_to(di_object *nonnull, di_string name,
                                                 di_object *nullable h);

/// Emit a signal with `name`, and `args`. The emitter of the signal is responsible of
/// freeing `args`.
PUBLIC_DEAI_API int di_emitn(di_object *nonnull, di_string name, di_tuple args);
/// Call object dtor, remove all public members from the object. Listeners are not
/// removed, they can only be removed when the object's strong refcount drop to 0
PUBLIC_DEAI_API void di_finalize_object(di_object *nonnull);

PUBLIC_DEAI_API di_object *nonnull allocates(malloc) di_ref_object(di_object *nonnull);

/// Create a weak reference to a di_object. The object could be NULL, in that case, an
/// empty reference is created.
PUBLIC_DEAI_API struct di_weak_object *nonnull di_weakly_ref_object(di_object *nullable);

/// Upgrade a weak object reference to an object reference.
///
/// @return An object reference, or NULL if the object has been freed.
PUBLIC_DEAI_API di_object *
    nullable allocates(malloc) di_upgrade_weak_ref(di_weak_object *nonnull);

/// Drop a weak object reference. After this function returns, the passed pointer will
/// become invalid
PUBLIC_DEAI_API void di_drop_weak_ref(di_weak_object *nonnull *nonnull);
static inline void di_drop_weak_ref_rvalue(di_weak_object *nonnull weak) {
	di_drop_weak_ref(&weak);
}

PUBLIC_DEAI_API void frees(malloc, 1) di_unref_object(di_object *nonnull);

PUBLIC_DEAI_API void di_set_object_dtor(di_object *nonnull, di_dtor_fn nullable);
PUBLIC_DEAI_API void di_set_object_call(di_object *nonnull, di_call_fn nullable);
PUBLIC_DEAI_API bool di_is_object_callable(di_object *nonnull);
/// Convert an object to its string representation. This function looks at the `__to_string`
/// member of the object. If it is a string, then it will be returned directly. Otherwise,
/// we try to call `__to_string` as a function and use its return value as the string representation.
/// `__to_string` must return a string directly, we don't support chaining.
PUBLIC_DEAI_API di_string di_object_to_string(di_object *nonnull o);
PUBLIC_DEAI_API di_array di_get_all_member_names_raw(di_object *nonnull obj_);
typedef bool (*nonnull di_member_cb)(di_string name, di_type, di_value *nonnull value,
                                     void *nullable data);
PUBLIC_DEAI_API bool
di_foreach_member_raw(di_object *nonnull obj_, di_member_cb cb, void *nullable user_data);
/// Iterate over all members of an object. If a member named `__next` exists in the object,
/// it will be called to get the next member. Otherwise, the raw member list will be used.
/// By default, members whose names start with "__" are considered internal members, and
/// will not be returned. This behavior is not enforced if `__next` is defined.
PUBLIC_DEAI_API di_tuple di_object_next_member(di_object *nonnull obj, di_string name);

PUBLIC_DEAI_API void di_free_tuple(di_tuple);
PUBLIC_DEAI_API void di_free_array(di_array);

/// Free a `value` of type `t`. This function does not free the storage space used by
/// `value`. This is to make this function usable for values stored on the stack.
PUBLIC_DEAI_API void di_free_value(di_type t, di_value *nullable value_ptr);

static inline void unused di_free_variant(di_variant v) {
	di_free_value(v.type, v.value);
	free(v.value);
}

/// Copy value of type `t` from `src` to `dst`. It's assumed that `dst` has enough
/// memory space to hold a value of type `t`, and that `dst` doesn't contain a
/// valid value beforehand
PUBLIC_DEAI_API void di_copy_value(di_type t, void *nullable dst, const void *nullable src);

/// Rename the signal object `old_member_name` to `new_member_name`. Both names
/// have to start with "__signal_", getter/setter/deleters are not used. This
/// takes care of updating the metadata fields in the signal object
PUBLIC_DEAI_API int di_rename_signal_member_raw(di_object *nonnull obj, di_string old_member_name,
                                                di_string new_member_name);
/// Duplicate null terminated string `str` into a di_string
static inline di_string unused di_string_dup(const char *nullable str) {
	return (di_string){
	    .data = str != NULL ? strdup(str) : NULL,
	    .length = str != NULL ? strlen(str) : 0,
	};
}

/// Duplicate exactly `length` bytes from `str` into a di_string
static inline di_string unused di_string_ndup(const char *nonnull str, size_t length) {
	__auto_type dup = (char *)malloc(length);
	memcpy(dup, str, length);
	return (di_string){
	    .data = dup,
	    .length = length,
	};
}

static inline di_string unused di_clone_string(di_string other) {
	return di_string_ndup(other.data, other.length);
}

/// Takes the ownership of a null terminated string `str` into a di_string
static inline di_string unused di_string_borrow(const char *nonnull str) {
	return (di_string){
	    .data = str,
	    .length = strlen(str),
	};
}

#define di_string_borrow_literal(str)                                                    \
	((di_string){.data = str, .length = sizeof(str) - 1})

static inline bool unused di_string_to_chars(di_string str, char *nonnull output, size_t capacity) {
	if (capacity < str.length + 1) {
		return false;
	}
	memcpy(output, str.data, str.length);
	output[str.length] = '\0';
	return true;
}

/// Split `str` at the first appearence of `sep`, returns if `sep` is found. If
/// `sep` is not found, `head` and `rest` will not be touched
static inline bool unused di_string_split_once(di_string str, char sep, di_string *nonnull head,
                                               di_string *nonnull rest) {
	const char *pos = (const char *)memchr(str.data, sep, str.length);
	if (pos == NULL) {
		return false;
	}
	*head = (di_string){
	    .data = str.data,
	    .length = (size_t)(pos - str.data),
	};
	*rest = (di_string){
	    .data = pos + 1,
	    .length = str.length - head->length - 1,
	};
	return true;
}

static inline char *nullable unused di_string_to_chars_alloc(di_string str) {
	if (str.length == 0) {
		return NULL;
	}
	__auto_type ret = (char *)malloc(str.length + 1);
	di_string_to_chars(str, ret, str.length + 1);
	return ret;
}

static inline di_string unused di_string_tolower(di_string str) {
	__auto_type ret = (char *)malloc(str.length);
	for (size_t i = 0; i < str.length; i++) {
		ret[i] = (char)tolower(str.data[i]);
	}
	return (di_string){.data = ret, .length = str.length};
}

static inline unused bool di_string_starts_with(di_string str, const char *nonnull pat) {
	size_t len = strlen(pat);
	if (str.length < len || strncmp(str.data, pat, len) != 0) {
		return false;
	}
	return true;
}

static inline unused bool di_string_starts_with_string(di_string str, di_string pat) {
	if (str.length < pat.length) {
		return false;
	}
	return memcmp(str.data, pat.data, pat.length) == 0;
}

static inline unused di_string di_string_concat(di_string a, di_string b) {
	di_string ret = {.data = NULL, .length = a.length + b.length};
	ret.data = (const char *)malloc(ret.length);
	memcpy((void *)ret.data, a.data, a.length);
	memcpy((void *)(ret.data + a.length), b.data, b.length);
	return ret;
}

static inline di_string unused di_string_vprintf(const char *nonnull fmt, va_list args) {
	di_string ret;
	ret.length = vasprintf((char **)&ret.data, fmt, args);        // minus the null byte
	return ret;
}

static inline di_string unused __attribute__((format(printf, 1, 2)))
di_string_printf(const char *nonnull fmt, ...) {
	di_string ret;
	va_list args;
	va_start(args, fmt);
	ret = di_string_vprintf(fmt, args);
	va_end(args);
	return ret;
}

static inline bool di_string_eq(di_string a, di_string b) {
	if (a.length != b.length) {
		return false;
	}
	return memcmp(a.data, b.data, a.length) == 0;
}

/// Get a substring of `str`, starting from `start`. `str` will be borrowed.
static inline di_string unused di_suffix(di_string str, size_t start) {
	if (start >= str.length) {
		return DI_STRING_INIT;
	}
	return (di_string){
	    .data = str.data + start,
	    .length = str.length - start,
	};
}
/// Get a substring of `str`, starting from `start`, with `length`. `str` will be borrowed.
static inline di_string unused di_substring(di_string str, size_t start, size_t len) {
	if (start >= str.length || len == 0) {
		return DI_STRING_INIT;
	}
	if (start + len > str.length) {
		len = str.length - start;
	}
	return (di_string){
	    .data = str.data + start,
	    .length = len,
	};
}

static inline void unused di_free_string(di_string str) {
	free((char *)str.data);
}

static inline void unused di_free_di_stringp(di_string *nonnull str) {
	free((char *)str->data);
	str->data = NULL;
	str->length = 0;
}

static inline void unused di_free_di_tuplep(di_tuple *nonnull t) {
	di_free_tuple(*t);
	t->elements = NULL;
	t->length = 0;
}

static inline void unused di_free_di_arrayp(di_array *nonnull a) {
	di_free_array(*a);
	a->arr = NULL;
	a->length = 0;
}

static inline void unused di_free_di_variantp(di_variant *nonnull v) {
	di_free_variant(*v);
	v->value = NULL;
	v->type = DI_TYPE_NAME(NIL);
}

static inline unused size_t di_sizeof_type(di_type t) {
	switch (t) {
	case DI_TYPE_NAME(NIL):
		return 0;
	case DI_TYPE_NAME(ANY):
	case DI_LAST_TYPE:
		abort();
	case DI_TYPE_NAME(FLOAT):
		return sizeof(double);
	case DI_TYPE_NAME(ARRAY):
		return sizeof(di_array);
	case DI_TYPE_NAME(TUPLE):
		return sizeof(di_tuple);
	case DI_TYPE_NAME(VARIANT):
		return sizeof(di_variant);
	case DI_TYPE_NAME(UINT):
	case DI_TYPE_NAME(INT):
		return sizeof(int64_t);
	case DI_TYPE_NAME(NUINT):
		return sizeof(unsigned int);
	case DI_TYPE_NAME(NINT):
		return sizeof(int);
	case DI_TYPE_NAME(STRING):
		return sizeof(di_string);
	case DI_TYPE_NAME(STRING_LITERAL):
	case DI_TYPE_NAME(OBJECT):
	case DI_TYPE_NAME(EMPTY_OBJECT):
	case DI_TYPE_NAME(POINTER):
		return sizeof(void *);
	case DI_TYPE_NAME(WEAK_OBJECT):
		return sizeof(struct di_weak_object *);
	case DI_TYPE_NAME(BOOL):
		return sizeof(bool);
	}
	abort();
}

/// A valid but non-upgradeable weak reference
PUBLIC_DEAI_API extern di_weak_object *const nonnull dead_weak_ref;

#ifndef __cplusplus
// Workaround for _Generic limitations, see:
// http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1930.htm
#define di_typeid(x)                                                                     \
	_Generic((x *)0,                                                                     \
	    di_array *: DI_TYPE_ARRAY,                                                       \
	    di_tuple *: DI_TYPE_TUPLE,                                                       \
	    di_variant *: DI_TYPE_VARIANT,                                                   \
	    int *: DI_TYPE_NINT,                                                             \
	    unsigned int *: DI_TYPE_NUINT,                                                   \
	    int64_t *: DI_TYPE_INT,                                                          \
	    uint64_t *: DI_TYPE_UINT, /* You need to return a `char *` if you returns an owned string,                                                                                 \
	                                 and you should cast to `char *` when capturing a string OR a string literal.                                                                    \
	                                 this is because a borrowed string literal could be long OR short lived. so you                                                             \
	                                 have to capture owned string to be safe.            \
	                               */                                                    \
	    di_string *: DI_TYPE_STRING, /* use a const to differentiate strings and string literals                                                                                \
	                                  * doesn't mean strings are actually mutable.       \
	                                  */                                                 \
	    const char **: DI_TYPE_STRING_LITERAL,                                           \
	    di_object **: DI_TYPE_OBJECT,                                                    \
	    di_weak_object **: DI_TYPE_WEAK_OBJECT,                                          \
	    void **: DI_TYPE_POINTER,                                                        \
	    double *: DI_TYPE_FLOAT,                                                         \
	    void *: DI_TYPE_NIL,                                                             \
	    bool *: DI_TYPE_BOOL)

#define di_signal_member_of_(sig) "__signal_" sig
#define di_signal_member_of(sig) (di_signal_member_of_(sig))
#define di_signal_setter_of(sig) ("__set_" di_signal_member_of_(sig))
#define di_signal_deleter_of(sig) ("__delete_" di_signal_member_of_(sig))

#define di_typeof(expr) di_typeid(typeof(expr))

#define di_set_return(v)                                                                 \
	do {                                                                                 \
		*rtype = di_typeof(v);                                                           \
		typeof(v) *retv;                                                                 \
		if (!*ret)                                                                       \
			*ret = calloc(1, di_min_return_size(sizeof(v)));                             \
		retv = *(typeof(v) **)ret;                                                       \
		*retv = v;                                                                       \
	} while (0);

#define define_object_cleanup(object_type)                                                  \
	static inline void unused di_free_##object_type##pp(object_type *nullable *nonnull p) { \
		if (*p) {                                                                           \
			di_unref_object((di_object *)*p);                                               \
		}                                                                                   \
		*p = NULL;                                                                          \
	}
#define scopedp(t) with_cleanup(di_free_##t##pp) t
#define scoped(t) with_cleanup(di_free_##t##p) t

unused define_object_cleanup(di_object);

static inline void unused di_free_di_weak_objectpp(di_weak_object *nullable *nonnull p) {
	if (*p) {
		di_drop_weak_ref(p);
	}
}

#define scoped_di_object scopedp(di_object)
#define scoped_di_weak_object scopedp(di_weak_object)
#define scoped_di_string scoped(di_string)
#define scoped_di_tuple scoped(di_tuple)
#define scoped_di_array scoped(di_array)
#define scoped_di_variant scoped(di_variant)

#define di_has_member(o, name)                                                           \
	(di_lookup((di_object *)(o), di_string_borrow(name)) != NULL)
#define di_emit(o, name, ...)                                                            \
	di_emitn((di_object *)o, di_string_borrow(name), di_make_tuple(__VA_ARGS__))

#define di_make_variant(x)                                                               \
	((struct di_variant){                                                                \
	    (di_value *)addressof(x),                                                        \
	    di_typeof(x),                                                                    \
	})
#define di_make_tuple(...)                                                               \
	((di_tuple){VA_ARGS_LENGTH(__VA_ARGS__),                                             \
	            (struct di_variant[]){LIST_APPLY(di_make_variant, SEP_COMMA, __VA_ARGS__)}})
// call but ignore return
#define di_call(o, name, ...)                                                            \
	({                                                                                   \
		int __rc = 0;                                                                    \
		do {                                                                             \
			di_type __rtype;                                                             \
			di_value __ret;                                                              \
			bool __called;                                                               \
			__rc = di_callx((di_object *)(o), di_string_borrow(name), &__rtype, &__ret,  \
			                di_make_tuple(__VA_ARGS__), &__called);                      \
			if (__rc != 0) {                                                             \
				break;                                                                   \
			}                                                                            \
			di_free_value(__rtype, &__ret);                                              \
		} while (0);                                                                     \
		__rc;                                                                            \
	})

#define di_callr(o, name, r, ...)                                                        \
	({                                                                                   \
		int __deai_callr_rc = 0;                                                         \
		do {                                                                             \
			di_type __deai_callr_rtype;                                                  \
			di_value __deai_callr_ret;                                                   \
			bool called;                                                                 \
			__deai_callr_rc =                                                            \
			    di_callx((di_object *)(o), di_string_borrow(name), &__deai_callr_rtype,  \
			             &__deai_callr_ret, di_make_tuple(__VA_ARGS__), &called);        \
			if (__deai_callr_rc != 0) {                                                  \
				break;                                                                   \
			}                                                                            \
			if (di_typeof(r) != __deai_callr_rtype) {                                    \
				di_free_value(__deai_callr_rtype, &__deai_callr_ret);                    \
				__deai_callr_rc = -EINVAL;                                               \
				break;                                                                   \
			}                                                                            \
			(r) = *(typeof(r) *)&__deai_callr_ret;                                       \
		} while (0);                                                                     \
		__deai_callr_rc;                                                                 \
	})

#define di_get2(o, prop, r)                                                              \
	({                                                                                   \
		int rc;                                                                          \
		do {                                                                             \
			rc = di_getxt((void *)(o), (prop), di_typeof(r), (di_value *)&(r));          \
			if (rc != 0) {                                                               \
				break;                                                                   \
			}                                                                            \
		} while (0);                                                                     \
		rc;                                                                              \
	})

#define di_get(o, prop, r) di_get2(o, di_string_borrow(prop), r)

/// Get a raw member of an object, returning the value without cloning it.
/// This is only possible with raw members, because getter returns are temporary values.
/// And the type of `r` has to match exactly the type of the member, no conversion is performed.
#define di_rawget_borrowed2(o, prop, r)                                                  \
	({                                                                                   \
		int __di_rawget__borrowd2_rc;                                                    \
		di_type __di_rawget__borrowd2_rtype;                                             \
		di_value *__di_rawget__borrowd2_ret;                                             \
		do {                                                                             \
			__di_rawget__borrowd2_rc =                                                   \
			    di_refrawgetx((void *)(o), (prop), &__di_rawget__borrowd2_rtype,         \
			                  &(__di_rawget__borrowd2_ret));                             \
			if (__di_rawget__borrowd2_rc != 0) {                                         \
				break;                                                                   \
			}                                                                            \
			if (__di_rawget__borrowd2_rtype != di_typeof(r)) {                           \
				__di_rawget__borrowd2_rc = -ERANGE;                                      \
				break;                                                                   \
			}                                                                            \
			memcpy(&(r), __di_rawget__borrowd2_ret,                                      \
			       di_sizeof_type(__di_rawget__borrowd2_rtype));                         \
		} while (0);                                                                     \
		__di_rawget__borrowd2_rc;                                                        \
	})
#define di_rawget_borrowed(o, prop, r) di_rawget_borrowed2(o, di_string_borrow(prop), r)
#endif
#undef DI_LAST_TYPE
#undef DI_TYPE_NAME
