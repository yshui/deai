#pragma once
#include <stdbool.h>
#include "common.h"
#include "object.h"

/// Create a new error object with the given message, file name, line number, and function
/// name. `file` and `func` may be NULL. `line` may be a non-positive number if the line
/// number is not known.
PUBLIC_DEAI_API di_object *ret_nonnull di_new_error_from_string(const char *nullable file,
                                                                int line,
                                                                const char *nullable func,
                                                                di_string message);
/// Like `di_new_error_from_string`, but creates the message from a format string and arguments.
PUBLIC_DEAI_API di_object *ret_nonnull di_new_error2(const char *nullable file, int line,
                                                     const char *nullable func,
                                                     const char *nonnull fmt, ...);
PUBLIC_DEAI_API bool di_is_error(di_object *nonnull obj);
PUBLIC_DEAI_API void noret di_throw(di_object *nonnull err);

#ifndef __cplusplus
/// Wrapper for `di_new_error2` that automatically fills in the file, line, and function.
#define di_new_error(...) di_new_error2(__FILE__, __LINE__, __func__, __VA_ARGS__)

/// Abort the current executing di_closure, and return an error. You should only use this
/// in functions wrapped in di_closures (see `di_create_closure`).
///
/// This macro does not return, but your local variables will still be cleaned up, if you
/// set the cleanup attribute for them
#define di_error(fmt, ...) (di_throw(di_new_error(fmt, ##__VA_ARGS__)))
#endif
