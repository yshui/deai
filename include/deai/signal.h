/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

#include <stdarg.h>

#include <deai/object.h>

struct di_signal {
	struct di_object;

	int nargs;
	int (*new)(struct di_signal *);
	void (*remove)(struct di_signal *);
	void *pad[2];

	di_type_t types[];
};
struct di_listener;

struct di_listener *di_add_listener_to_signal(struct di_signal *, struct di_object *o);
int di_remove_listener_from_signal(struct di_signal *, struct di_listener *o);
void di_stop_listener(struct di_listener *l);
void di_bind_listener(struct di_listener *l, struct di_object *emitter);
struct di_signal *di_new_signal(int nargs, di_type_t *types);

void di_disarm(struct di_signal *);

int di_emitn(struct di_signal *sig, struct di_object *emitter, const void *const *args);
int di_emitv(struct di_signal *sig, struct di_object *emitter, va_list ap);
int di_emit(struct di_signal *sig, struct di_object *emitter, ...);
