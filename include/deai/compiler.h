/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

//TODO: Check compiler versions

#define NONNULL_ARG(...) __attribute__((nonnull(__VA_ARGS__)))
#define NONNULL_ALL __attribute__((nonnull))
#define UNUSED __attribute__((unused))

#ifndef __has_feature
# define __has_feature(x) 0
#endif

#if !__has_feature(nullability)
# define _Nonnull
# define _Nullable
# define _Null_unspecified
#endif
