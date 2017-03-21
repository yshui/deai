#pragma once

#include <ffi.h>
#include <deai.h>
#include <errno.h>

typedef void (*init_fn_t)(struct di_module *);

struct di_module *di_module_lookup(struct deai *, const char *);
struct di_module *di_modules(struct deai *);
struct di_module *di_module_next(struct di_module *pm);

struct di_fn *di_module_function_lookup(struct di_module *, const char *);
struct di_fn *di_module_functions(struct di_module *);
struct di_fn *di_module_function_next(struct di_fn *);

struct di_module *di_module_new(const char *name);
int di_register_module(struct deai *, struct di_module *);
int di_module_register_function(struct di_module *, ffi_cif *, void (*)(void), const char *);
