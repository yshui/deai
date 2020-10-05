/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

//TODO: Check compiler versions

#define vattr(name, ...) __attribute__((name(__VA_ARGS__)))

#define nonnull_args(...) vattr(__nonnull__, __VA_ARGS__)
#define nonnull_all __attribute__((__nonnull__))
#define unused __attribute__((unused))
#define ret_nonnull nonnull __attribute__((returns_nonnull))
#define unreachable() __builtin_unreachable()

#ifndef __has_feature
# define __has_feature(x) 0
#endif

#if !__has_feature(nullability)
# define nonnull
# define nullable
# define null_unspecified
#else
# define nonnull _Nonnull
# define nullable _Nullable
# define null_unspecified _Null_unspecified
#endif

#define PUBLIC __attribute__((visibility("default")))
