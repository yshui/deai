#pragma once

// XXX merge into deai.h

#include <deai.h>
#include <errno.h>

typedef void (*init_fn_t)(struct deai *);

struct di_module *di_find_module(struct deai *, const char *);
struct di_module *di_get_modules(struct deai *);
struct di_module *di_next_module(struct di_module *pm);

struct di_method *di_find_method(struct di_object *, const char *);
struct di_method *di_get_methods(struct di_object *);
struct di_method *di_next_method(struct di_method *);

struct di_module *di_new_module(const char *name, size_t);
void di_free_object(struct di_object *);
int di_register_module(struct deai *, struct di_module *);
int di_register_method(struct di_object *, struct di_method *);
int di_register_typed_method(struct di_object *, struct di_typed_method *);
struct di_typed_method *
di_create_typed_method(di_fn_t fn, const char *name, di_type_t rtype,
                       unsigned int nargs, ...);
struct di_untyped_method *
di_create_untyped_method(di_callbale_t fn, const char *name, void *user_data);

#define di_new_module_with_type(name, type) (type *)di_new_module(name, sizeof(type))

int di_call_callable(struct di_callable *c, di_type_t *rtype, void **ret,
                     unsigned int nargs, const di_type_t *atypes,
                     const void *const *args);
int di_call_callable_v(struct di_callable *c, di_type_t *rtype, void **ret, ...);

