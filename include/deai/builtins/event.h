/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2022 Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once
#include "../object.h"

struct di_promise;
PUBLIC_DEAI_API void di_resolve_promise(struct di_promise *, struct di_variant);
PUBLIC_DEAI_API struct di_object *
di_any_promise(struct di_object *event_module, struct di_array promises);
PUBLIC_DEAI_API struct di_object *
di_collect_promises(struct di_object *event_module, struct di_array promises);
PUBLIC_DEAI_API struct di_object *
di_promise_then(struct di_object *promise, struct di_object *handler);
PUBLIC_DEAI_API struct di_object *di_new_promise(struct di_object *event_module);
