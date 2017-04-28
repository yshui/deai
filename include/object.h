#pragma once

// XXX merge into deai.h

#include <errno.h>

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
	const char *name;
	struct deai *di;
	char padding[56];
};

struct di_listener_data {
	struct di_object *obj;
	void *user_data;
};

void di_free_object(struct di_object *);
int di_register_method(struct di_object *, struct di_method *);
int di_register_typed_method(struct di_object *, struct di_typed_method *);
struct di_typed_method *
di_create_typed_method(di_fn_t fn, const char *name, di_type_t rtype,
                       unsigned int nargs, ...);
struct di_untyped_method *
di_create_untyped_method(di_callbale_t fn, const char *name, void *user_data);

int di_call_callable(struct di_callable *c, di_type_t *rtype, void **ret,
                     unsigned int nargs, const di_type_t *atypes,
                     const void *const *args);
int di_call_callable_v(struct di_callable *c, di_type_t *rtype, void **ret, ...);

struct di_listener *
di_add_typed_listener(struct di_object *, const char *name, void *ud, di_fn_t f);
struct di_listener *
di_add_untyped_listener(struct di_object *obj, const char *name, void *ud,
                        void (*f)(struct di_signal *, void **));

struct di_object *di_new_object(size_t sz);
void di_destroy_object(struct di_object *);
void di_ref_object(struct di_object *);
void di_unref_object(struct di_object *);

/**
 * Remove a listener from signal
 *
 * Returns the user_data passed to add_listener if succeed, otherwise
 * returns an error code.
 */
void *
di_remove_listener(struct di_object *o, const char *name, struct di_listener *l);
int di_emit_signal(struct di_object *, const char *name, void **args);
int di_emit_signal_v(struct di_object *obj, const char *name, ...);
int di_register_signal(struct di_object *, const char *name, int nargs, ...);
const di_type_t *di_get_signal_arg_types(struct di_signal *sig, unsigned int *nargs);
struct di_object *di_new_error(const char *errmsg);

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

// Workaround for _Generic limitations, see:
// http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1930.htm
#define di_typeof(x)                                                                \
	_Generic((x)+0, \
	int: DI_TYPE_NINT, \
	unsigned int: DI_TYPE_NUINT, \
	int64_t: DI_TYPE_INT, \
	uint64_t: DI_TYPE_UINT, \
	struct di_array: DI_TYPE_ARRAY, \
	char *: DI_TYPE_STRING, \
	const char *: DI_TYPE_STRING, \
	struct di_object *: DI_TYPE_OBJECT, \
	void *: DI_TYPE_POINTER, \
	double: DI_TYPE_FLOAT \
)
#define di_type_from_c(type)                                                        \
	({                                                                          \
		type __tmp;                                                         \
		di_typeof(__tmp);                                                   \
	})
#define di_set_return(v)                                                            \
	do {                                                                        \
		*rtype = di_typeof(v);                                              \
		typeof(v) *retv = calloc(1, sizeof(v));                             \
		*retv = v;                                                          \
		*ret = retv;                                                        \
	} while (0);
