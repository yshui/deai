/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

#include <deai/compiler.h>

#include <stdbool.h>
#include <stddef.h>

/// An append only string buffer
struct string_buf;

/// Create a new string buffer
struct string_buf *allocates(malloc) string_buf_new(void);
/// Append a string `str` with length `len` to the string buffer. If the first `len` bytes
//of `str` contains null bytes, up until the first null byte will be pushed.
void string_buf_lpush(struct string_buf *, const char *str, size_t len);
/// Append a null terminated string `str` to the string buffer
void string_buf_push(struct string_buf *, const char *str);
/// Dump the content of the string buffer into an char array, and clear the content
/// of the string buffer
char *string_buf_dump(struct string_buf *);
/// Clear the content of the string buffer
void string_buf_clear(struct string_buf *);
/// Returns whether the string buffer is empty
bool string_buf_is_empty(struct string_buf *);
