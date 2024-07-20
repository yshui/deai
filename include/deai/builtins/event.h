/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2022 Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once
#include "../object.h"

struct di_promise;
PUBLIC_DEAI_API void di_promise_resolve(di_object *, struct di_variant);
PUBLIC_DEAI_API void di_promise_reject(di_object *promise, di_object *);
PUBLIC_DEAI_API di_object *di_any_promise(di_object *event_module, di_array promises);
PUBLIC_DEAI_API di_object *di_join_promises(di_object *event_module, di_array promises);
PUBLIC_DEAI_API di_object *di_promise_then(di_object *promise, di_object *handler);
PUBLIC_DEAI_API di_object *di_promise_catch(di_object *promise, di_object *handler);
PUBLIC_DEAI_API di_object *di_new_promise(di_object *event_module);
