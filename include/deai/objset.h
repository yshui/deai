/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

#include <deai/object.h>

struct di_objset;

int di_hold_object(struct di_objset *, struct di_object *obj);
int di_release_object(struct di_objset *, const struct di_object *obj);
void di_release_all_objects(struct di_objset *);
struct di_objset *di_new_objset(void);
