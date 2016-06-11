#pragma once

#include <ffi.h>
#include <piped.h>
#include <errno.h>

typedef void (*init_fn_t)(struct piped_module *);

struct piped_module *piped_module_lookup(struct piped *, const char *);
struct piped_module *piped_modules(struct piped *);
struct piped_module *piped_module_next(struct piped_module *pm);

struct piped_fn *piped_module_function_lookup(struct piped_module *, const char *);
struct piped_fn *piped_module_functions(struct piped_module *);
struct piped_fn *piped_module_function_next(struct piped_fn *);

struct piped_module *piped_module_new(const char *name);
int piped_register_module(struct piped *, struct piped_module *);
int piped_module_register_function(struct piped_module *, ffi_cif *, void (*)(void), const char *);
