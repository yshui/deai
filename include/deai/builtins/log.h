/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once
#include "../object.h"
#include "../common.h"
#include "../compiler.h"

enum di_log_level {
	DI_LOG_ERROR,
	DI_LOG_WARN,
	DI_LOG_INFO,
	DI_LOG_DEBUG,
};

PUBLIC_DEAI_API __attribute__((format(printf, 3, 4))) int
di_log_va(di_object *nullable o, int log_level, const char *nonnull fmt, ...);
PUBLIC_DEAI_API int di_set_log_level(di_object *nonnull o, int log_level);

PUBLIC_DEAI_API extern di_object *nullable log_module;
