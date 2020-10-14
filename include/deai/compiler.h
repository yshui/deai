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
#ifdef NDEBUG
#define unreachable() __builtin_unreachable()
#else
#define unreachable() __builtin_trap()
#endif

#ifndef __has_feature
# define __has_feature(x) 0
#endif

#ifndef __has_attribute
# define __has_attribute(x) 0
#endif

#if __has_attribute(ownership_returns)
# define allocates(name) __attribute__((ownership_returns(name)))
# define frees(name, id) __attribute__((ownership_takes(name, id)))
# define holds(name, id) __attribute__((ownership_holds(name, id)))
#else
# define allocates(name)
# define frees(name, id)
# define holds(name, id)
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

#define visibility_default __attribute__((visibility("default")))
#define with_cleanup_t(type) __attribute__((cleanup(free_##type##pp))) type *
#define with_cleanup(func) __attribute__((cleanup(func)))
