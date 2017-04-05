#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

struct deai;

typedef enum di_type {
	DI_TYPE_VOID = 0,
	DI_TYPE_NINT,            // native int
	DI_TYPE_NUINT,           // native unsigned int
	DI_TYPE_UINT,            // uint64_t
	DI_TYPE_INT,             // int64_t
	DI_TYPE_FLOAT,           // platform dependent, double
	DI_TYPE_POINTER,         // Generic pointer, void *
	DI_TYPE_OBJECT,          // pointer to di_object
	DI_TYPE_STRING,          // utf-8 string, const char *
	DI_TYPE_ARRAY,           // struct di_array
	DI_TYPE_CALLABLE,        // pointer to di_callable
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
struct di_listener;
struct di_callable;
struct di_object {
	struct di_method *fn;
	struct di_signal *evd;

	uint64_t ref_count;
};

struct di_array {
	uint64_t length;
	void *arr;
	uint8_t elem_type;
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

typedef int (*di_closure)(struct di_typed_method *fn, void *ret, void **args,
                          void *user_data);

struct di_listener *
di_add_typed_listener(struct di_object *, const char *name, void *ud, di_fn_t f);
struct di_listener *
di_add_untyped_listener(struct di_object *obj, const char *name, void *ud,
                        void (*f)(struct di_signal *, void **));

void di_init_object(struct di_object *obj);
void di_ref_object(struct di_object *);
void di_unref_object(struct di_object *);

/**
 * Remove a listener from signal
 *
 * Returns the user_data passed to add_listener if succeed, otherwise
 * returns an error code.
 */
void *di_remove_listener(struct di_object *o, const char *name, struct di_listener *l);
int di_emit_signal(struct di_object *, const char *name, void **args);
int di_emit_signal_v(struct di_object *obj, const char *name, ...);
int di_register_signal(struct di_object *, const char *name, int nargs, ...);
const di_type_t *
di_get_signal_arg_types(struct di_signal *sig, unsigned int *nargs);
static inline size_t di_sizeof_type(di_type_t t) {
	switch (t) {
	case DI_TYPE_VOID:
	case DI_TYPE_CALLABLE:
	case DI_LAST_TYPE:
	default: return 0;
	case DI_TYPE_FLOAT: return sizeof(double);
	case DI_TYPE_ARRAY: return sizeof(struct di_array);
	case DI_TYPE_UINT:
	case DI_TYPE_INT: return 8;
	case DI_TYPE_NUINT: return sizeof(unsigned int);
	case DI_TYPE_NINT: return sizeof(int);
	case DI_TYPE_STRING:
	case DI_TYPE_OBJECT:
	case DI_TYPE_POINTER: return sizeof(void *);
	}
}
