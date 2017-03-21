#pragma once
#include <stdbool.h>
#include <stdlib.h>

struct deai;
struct di_module;
struct di_evsrc;
struct di_evsrc_reg;

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
	DI_TYPE_POINTER,       //Generic pointer
	DI_TYPE_EVENT_SOURCE,  //struct di_evsrc*
	DI_TYPE_STRING,        //char*
	DI_LAST_TYPE
} di_type_t;

struct di_fn {
	const char *name;
	unsigned int nargs;
	di_type_t rtype;
	const di_type_t *atypes;
	const void (*fn_ptr)(void);
};

struct di_event_desc {
	const char *name;
	unsigned int nargs;
	const di_type_t *types;
};

#define MAX_ERRNO 4095

static inline void * ERR_PTR(long err) {
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

typedef int (*di_closure)(struct di_fn *fn, void *ret, void **args, void *user_data);

struct di_evsrc *di_event_source_new(void);
int di_event_source_add_listener(struct di_evsrc *, const char *ev_name, struct di_fn *f);
int di_event_source_emit(struct di_evsrc *, const struct di_event_desc *, void **ev_data);

struct di_evsrc *di_core_event_source(struct deai *);
struct di_evsrc *di_module_event_source(struct di_module *);

int
di_event_source_registry_add_event(struct di_evsrc_reg *, const struct di_event_desc *);
