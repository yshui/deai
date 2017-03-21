#pragma once
#include <stdbool.h>
#include <stdlib.h>

struct deai;

typedef enum di_type {
	DI_TYPE_VOID = 0,
	DI_TYPE_UINT8,
	DI_TYPE_UINT16,
	DI_TYPE_UINT32,
	DI_TYPE_UINT64,
	DI_TYPE_INT8,
	DI_TYPE_INT16,
	DI_TYPE_INT32,
	DI_TYPE_INT64,
	DI_TYPE_FLOAT,
	DI_TYPE_DOUBLE,
	DI_TYPE_POINTER,        // Generic pointer
	DI_TYPE_OBJECT,
	DI_TYPE_STRING,        // utf-8 string
	DI_TYPE_ARRAY,
	DI_TYPE_CALLABLE,
	DI_LAST_TYPE
} di_type_t;

typedef void (*di_fn_t)(void);
typedef int (*di_callbale_t)(di_type_t *rtype, void **ret, unsigned int nargs,
                             const di_type_t *atypes, const void *const *args,
                             void *user_data);

struct di_callable {
	di_callbale_t fn_ptr;
};
struct di_typed_method;
struct di_untyped_method;
struct di_method {
	struct di_callable;
	const char *name;
};
struct di_signal;
struct di_callable;
struct di_object {
	struct di_method *fn;
	struct di_signal *evd;
};

struct di_array {
	di_type_t elem_type;
	size_t length;
	void *arr;
};

struct di_module {
	struct di_object;
	const char *name;
	struct deai *di;
	char padding[56];
};

struct di_listener_data {
	struct di_object *obj;
	void *user_data;
};

#define MAX_ERRNO 4095

static inline void *ERR_PTR(long err) {
	return (void *)err;
}

static inline long PTR_ERR(const void *ptr) {
	return (long)ptr;
}

#define unlikely(x) (__builtin_constant_p(x) ? !!(x) : __builtin_expect(!!(x), 0))

#define IS_ERR_VALUE(x) unlikely((x) >= (unsigned long)-MAX_ERRNO)

static inline bool IS_ERR(const void *ptr) {
	return IS_ERR_VALUE((unsigned long)ptr);
}

static inline bool IS_ERR_OR_NULL(const void *ptr) {
	return unlikely(!ptr) || IS_ERR_VALUE((unsigned long)ptr);
}

typedef int (*di_closure)(struct di_typed_method *fn, void *ret, void **args, void *user_data);

int di_add_listener(struct di_object *, const char *name, void *ud, di_fn_t *f);
int di_emit_signal(struct di_object *, const char *name, void **args);
int di_register_signal(struct di_object *, const char *name, int nargs, ...);
