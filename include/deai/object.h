/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

// XXX merge into deai.h

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
	DI_TYPE_NIL,
	DI_LAST_TYPE
} di_type_t;

typedef void (*di_fn_t)(void);

struct di_signal;
struct di_listener;
struct di_callable;
struct di_object {
	struct di_member *members;

	void (*dtor)(struct di_object *);
	int (*call)(struct di_object *, di_type_t *rt, void **ret, int nargs,
	            const di_type_t *atypes, const void *const *args);

	uint64_t ref_count;

	// If a object is destroyed, it's just a placeholder
	// waiting for its ref count to drop to 0
	uint8_t destroyed;        // 1 -> destroyed, 2 -> destroying
};

struct di_array {
	uint64_t length;
	void *arr;
	uint8_t elem_type;
};

struct di_module {
	struct di_object;
	struct deai *di;
	char padding[56];
};

struct di_member {
	const char *name;
	void *data;
	di_type_t type;
	bool writable;
	bool own;
};

int di_callx(struct di_object *o, const char *name, di_type_t *rt, void **ret, ...);
int di_rawcallx(struct di_object *o, const char *name, di_type_t *rt, void **ret, ...);
int di_rawcallxn(struct di_object *o, const char *name, di_type_t *rt, void **ret,
                 int nargs, const di_type_t *ats, const void *const *args);

int di_setx(struct di_object *o, const char *prop, di_type_t type, const void *ret);
int di_rawgetx(struct di_object *o, const char *prop, di_type_t *type,
               const void **ret);
int di_rawgetxt(struct di_object *o, const char *prop, di_type_t type,
                const void **ret);
int di_getx(struct di_object *o, const char *prop, di_type_t *type, const void **ret);
int di_getxt(struct di_object *o, const char *prop, di_type_t type, const void **ret);

int di_set_type(struct di_object *o, const char *type);
const char *di_get_type(struct di_object *o);
bool di_check_type(struct di_object *o, const char *type);

void di_free_object(struct di_object *);

struct di_member *di_alloc_member(void);
int di_add_address_member(struct di_object *o, const char *name, bool writable,
                          di_type_t t, void *address);
int di_add_value_member(struct di_object *o, const char *name, bool writable,
                        di_type_t t, ...);
struct di_member *di_find_member(struct di_object *o, const char *name);
struct di_object *di_new_object(size_t sz);
void di_destroy_object(struct di_object *);
void di_ref_object(struct di_object *);
void di_unref_object(struct di_object *);

const di_type_t *di_get_signal_arg_types(struct di_signal *sig, int *nargs);
struct di_object *di_new_error(const char *fmt, ...);
void di_free_array(struct di_array);
void di_free_value(di_type_t, void *);
void di_copy_value(di_type_t t, void *dest, const void *src);

size_t di_min_return_size(size_t);

static inline size_t di_sizeof_type(di_type_t t) {
	switch (t) {
	case DI_TYPE_VOID:
	case DI_LAST_TYPE:
	case DI_TYPE_NIL:
	default: return 0;
	case DI_TYPE_FLOAT: return sizeof(double);
	case DI_TYPE_ARRAY: return sizeof(struct di_array);
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
	int *: DI_TYPE_NINT, \
	unsigned int *: DI_TYPE_NUINT, \
	int64_t *: DI_TYPE_INT, \
	uint64_t *: DI_TYPE_UINT, \
	char **: DI_TYPE_STRING, \
	const char **: DI_TYPE_STRING, \
	struct di_object **: DI_TYPE_OBJECT, \
	void **: DI_TYPE_POINTER, \
	double *: DI_TYPE_FLOAT, \
	void *: DI_TYPE_VOID \
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

#define DI_ARRAY_NIL ((struct di_array){0, NULL, DI_TYPE_VOID})

#define define_object_cleanup(t)                                                    \
	static inline void free_##t(struct t **ptr) {                               \
		if (*ptr)                                                           \
			di_unref_object((void *)ptr);                               \
		*ptr = NULL;                                                        \
	}
