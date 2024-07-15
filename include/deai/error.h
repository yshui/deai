#pragma once
#include "common.h"
typedef struct di_object di_object;

PUBLIC_DEAI_API di_object *ret_nonnull di_new_error(const char *nonnull fmt, ...);
PUBLIC_DEAI_API bool di_is_error(di_object *nonnull obj);
PUBLIC_DEAI_API void noret di_throw(di_object *nonnull err);

/// Abort the current executing di_closure, and return an error. You should only use this
/// in functions wrapped in di_closures (see `di_create_closure`).
///
/// This macro does not return, but your local variables will still be cleaned up, if you
/// set the cleanup attribute for them
#define di_error(fmt, ...) (di_throw(di_new_error(fmt, ##__VA_ARGS__)))
