/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

#include <stdbool.h>

struct string_buf;

struct string_buf *string_buf_new(void);
void string_buf_lpush(struct string_buf *, const char *str, size_t len);
void string_buf_push(struct string_buf *, const char *str);
char *string_buf_dump(struct string_buf *);
void string_buf_clear(struct string_buf *);
bool string_buf_is_empty(struct string_buf *);
