/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

// XXX merge into deai.h

#include <deai/compiler.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef enum di_type {
	DI_TYPE_VOID = 0,
	DI_TYPE_BOOL,           // boolean, no implicit conversion to number types
	DI_TYPE_NINT,           // native int
	DI_TYPE_NUINT,          // native unsigned int
	DI_TYPE_UINT,           // uint64_t
	DI_TYPE_INT,            // int64_t
	DI_TYPE_FLOAT,          // platform dependent, double
	DI_TYPE_POINTER,        // Generic pointer, void *
	DI_TYPE_OBJECT,         // pointer to di_object
	DI_TYPE_STRING,         // utf-8 string, char *
	DI_TYPE_STRING_LITERAL,        // utf-8 string literal, const char *
	DI_TYPE_ARRAY,                 // struct di_array
	DI_TYPE_TUPLE,                 // array with variable element type
	DI_TYPE_NIL,
	DI_LAST_TYPE
} di_type_t;

struct di_tuple;
struct di_object;
typedef int (*di_call_fn_t)(struct di_object *nonnull, di_type_t *nonnull rt,
                            void *nullable *nonnull ret, struct di_tuple);
struct di_signal;
struct di_listener;
struct di_callable;
struct di_object {
	struct di_member *nullable members;
	struct di_signal *nullable signals;

	void (*nullable dtor)(struct di_object *nonnull);
	di_call_fn_t nullable call;

	uint64_t ref_count;
	uint8_t destroyed;
};

struct di_array {
	uint64_t length;
	void *nullable arr;
	di_type_t elem_type;
};

struct di_tuple {
	uint64_t length;
	void *nonnull *nullable tuple;
	di_type_t *nullable elem_type;
};

struct di_module {
	struct di_object;
	struct deai *nonnull di;
	char padding[56];
};

struct di_member {
	char *nonnull name;
	void *nonnull data;
	di_type_t type;
	bool writable;
	bool own;
};

int di_callx(struct di_object *nonnull o, const char *nonnull name,
             di_type_t *nonnull rt, void *nullable *nonnull ret, ...);
int di_rawcallx(struct di_object *nonnull o, const char *nonnull name,
                di_type_t *nonnull rt, void *nullable *nonnull ret, ...);
int di_rawcallxn(struct di_object *nonnull o, const char *nonnull name,
                 di_type_t *nonnull rt, void *nullable *nonnull ret,
                 struct di_tuple);

int di_setx(struct di_object *nonnull o, const char *nonnull prop, di_type_t type,
            void *nullable val);
int di_rawgetx(struct di_object *nonnull o, const char *nonnull prop,
               di_type_t *nonnull type, void *nullable *nonnull ret);
int di_rawgetxt(struct di_object *nonnull o, const char *nonnull prop,
                di_type_t type, void *nullable *nonnull ret);
int di_getx(struct di_object *nonnull no, const char *nonnull prop,
            di_type_t *nonnull type, void *nullable *nonnull ret);
int di_getxt(struct di_object *nonnull o, const char *nonnull prop, di_type_t type,
             void *nullable *nonnull ret);

int di_set_type(struct di_object *nonnull o, const char *nonnull type);
const char *nonnull di_get_type(struct di_object *nonnull o);
bool di_check_type(struct di_object *nonnull o, const char *nonnull type);

int nonnull_all
di_add_member(struct di_object *nonnull o, const char *nonnull name,
              bool writable, bool own, di_type_t, void *nonnull address);
int di_add_ref_member(struct di_object *nonnull o, const char *nonnull name,
                      bool writable, di_type_t t, void *nonnull address);
int di_add_value_member(struct di_object *nonnull o, const char *nonnull name,
                        bool writable, di_type_t t, ...);
int di_remove_member(struct di_object *nonnull o, const char *nonnull name);
struct di_member *nullable di_lookup(struct di_object *nonnull o,
                                      const char *nonnull name);
struct di_object *nullable di_new_object(size_t sz);

struct di_listener *nullable di_listen_to(struct di_object *nonnull o,
                                           const char *nonnull name,
                                           struct di_object *nullable h);
struct di_listener *nullable di_listen_to_once(struct di_object *nonnull o,
                                                const char *nonnull name,
                                                struct di_object *nullable h,
                                                bool once);

// Unscribe from a signal from the listener side. __detach is not called in this case
// It's guaranteed after calling this, the handler and __detach method will never be
// called
int di_stop_listener(struct di_listener *nullable);
int di_emitn(struct di_object *nonnull, const char *nonnull name, struct di_tuple);
// Call object dtor, remove all listeners and members from the object. And free the
// memory
// if the ref count drop to 0 after this process
void di_destroy_object(struct di_object *nonnull);

// Detach all listeners attached to object, __detach of listeners will be called
void di_clear_listeners(struct di_object *nonnull);

struct di_object *nonnull di_ref_object(struct di_object *nonnull);
void di_unref_object(struct di_object *nonnull);

void di_free_tuple(struct di_tuple);
void di_free_array(struct di_array);
void di_free_value(di_type_t, void *nonnull);
void di_copy_value(di_type_t t, void *nullable dest, const void *nullable src);

static inline size_t di_sizeof_type(di_type_t t) {
	switch (t) {
	case DI_TYPE_VOID:
	case DI_LAST_TYPE:
	case DI_TYPE_NIL:
	default: return 0;
	case DI_TYPE_FLOAT: return sizeof(double);
	case DI_TYPE_ARRAY: return sizeof(struct di_array);
	case DI_TYPE_TUPLE: return sizeof(struct di_tuple);
	case DI_TYPE_UINT:
	case DI_TYPE_INT: return 8;
	case DI_TYPE_NUINT: return sizeof(unsigned int);
	case DI_TYPE_NINT: return sizeof(int);
	case DI_TYPE_STRING:
	case DI_TYPE_STRING_LITERAL:
	case DI_TYPE_OBJECT:
	case DI_TYPE_POINTER: return sizeof(void *);
	case DI_TYPE_BOOL: return sizeof(bool);
	}
}

// Workaround for _Generic limitations, see:
// http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1930.htm
#define di_typeid(x)                                                                \
	_Generic((x*)0, \
	struct di_array *: DI_TYPE_ARRAY, \
	struct di_tuple *: DI_TYPE_TUPLE, \
	int *: DI_TYPE_NINT, \
	unsigned int *: DI_TYPE_NUINT, \
	int64_t *: DI_TYPE_INT, \
	uint64_t *: DI_TYPE_UINT, \
	char **: DI_TYPE_STRING, \
	const char **: DI_TYPE_STRING, \
	struct di_object **: DI_TYPE_OBJECT, \
	void **: DI_TYPE_POINTER, \
	double *: DI_TYPE_FLOAT, \
	void *: DI_TYPE_VOID, \
	bool *: DI_TYPE_BOOL \
)

#define di_typeof(expr) di_typeid(typeof(expr))

#define di_set_return(v)                                                            \
	do {                                                                        \
		*rtype = di_typeof(v);                                              \
		typeof(v) *retv;                                                    \
		if (!*ret)                                                          \
			*ret = calloc(1, di_min_return_size(sizeof(v)));            \
		retv = *(typeof(v) **)ret;                                          \
		*retv = v;                                                          \
	} while (0);

#define DI_ARRAY_NIL ((struct di_array){0, NULL, DI_TYPE_NIL})
#define DI_TUPLE_NIL ((struct di_tuple){0, NULL, NULL})

#define define_object_cleanup(t)                                                    \
	static inline void free_##t(struct t **ptr) {                               \
		if (*ptr)                                                           \
			di_unref_object((struct di_object *)*ptr);                  \
		*ptr = NULL;                                                        \
	}
#define with_object_cleanup(t) with_cleanup(free_##t) struct t *
