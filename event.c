/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai/builtin/event.h>
#include <deai/deai.h>
#include <deai/helper.h>

#include <ev.h>

#include "di_internal.h"
#include "event.h"
#include "utils.h"

struct di_ioev {
	struct di_object;
	ev_io evh;
	struct deai *di;
	struct di_listener *d;
};

struct di_timer {
	struct di_object;
	ev_timer evt;
	struct deai *di;
	struct di_listener *d;
};

struct di_periodic {
	struct di_object;
	ev_periodic pt;
	struct deai *di;
	struct di_listener *d;
};

struct di_evmodule {
	struct di_module;
};

static void di_ioev_callback(EV_P_ ev_io *w, int revents) {
	auto ev = container_of(w, struct di_ioev, evh);
	if (revents & EV_READ)
		di_emit(ev, "read");
	if (revents & EV_WRITE)
		di_emit(ev, "write");
}

static void di_timer_callback(EV_P_ ev_timer *t, int revents) {
	auto d = container_of(t, struct di_timer, evt);
	double now = ev_now(EV_A);
	ev_timer_stop(d->di->loop, t);
	di_emit(d, "elapsed", now);
}

static void di_periodic_callback(EV_P_ ev_periodic *w, int revents) {
	auto p = container_of(w, struct di_periodic, pt);
	double now = ev_now(EV_A);
	di_emit(p, "triggered", now);
}

static void di_start_ioev(struct di_object *obj) {
	struct di_ioev *ev = (void *)obj;
	if (!ev->di)
		return;
	ev_io_start(ev->di->loop, &ev->evh);
}

static void di_ioev_dtor(struct di_object *obj) {
	struct di_ioev *ev = (void *)obj;
	di_stop_unref_listenerp(&ev->d);
	ev_io_stop(ev->di->loop, &ev->evh);
	di_unref_object((void *)ev->di);
	ev->di = NULL;
}

static struct di_object *di_create_ioev(struct di_object *obj, int fd, int t) {
	struct di_evmodule *em = (void *)obj;
	auto ret = di_new_object_with_type(struct di_ioev);

	unsigned int flags = 0;
	if (t & IOEV_READ)
		flags |= EV_READ;
	if (t & IOEV_WRITE)
		flags |= EV_WRITE;

	ev_io_init(&ret->evh, di_ioev_callback, fd, flags);
	ret->di = em->di;
	di_ref_object((void *)ret->di);

	ret->d = di_listen_to_destroyed((void *)em->di, trivial_destroyed_handler, (void *)ret);

	di_method(ret, "start", di_start_ioev);

	ret->dtor = di_ioev_dtor;
	return (void *)ret;
}

static void di_timer_dtor(struct di_object *obj) {
	struct di_timer *ev = (void *)obj;
	di_stop_unref_listenerp(&ev->d);
	ev_timer_stop(ev->di->loop, &ev->evt);
	di_unref_object((void *)ev->di);
	ev->di = NULL;
}

static void di_timer_again(struct di_timer *obj) {
	if (!obj->di)
		return;

	ev_timer_again(obj->di->loop, &obj->evt);
}

static void di_timer_set(struct di_timer *obj, uint64_t t) {
	if (!obj->di)
		return;

	obj->evt.repeat = t;
	ev_timer_again(obj->di->loop, &obj->evt);
}

static struct di_object *di_create_timer(struct di_object *obj, uint64_t timeout) {
	struct di_evmodule *em = (void *)obj;
	auto ret = di_new_object_with_type(struct di_timer);
	ret->di = em->di;
	di_ref_object((void *)ret->di);

	ret->dtor = di_timer_dtor;
	di_method(ret, "start", di_timer_again);
	di_method(ret, "again", di_timer_again);

	// Set the timeout and restart the timer
	di_method(ret, "__set_timeout", di_timer_set, uint64_t);

	ret->d = di_listen_to_destroyed((void *)em->di, trivial_destroyed_handler, (void *)ret);

	ev_init(&ret->evt, di_timer_callback);
	ret->evt.repeat = timeout;
	return (void *)ret;
}

static void periodic_dtor(struct di_periodic *p) {
	di_stop_unref_listenerp(&p->d);
	ev_periodic_stop(p->di->loop, &p->pt);
	di_unref_object((void *)p->di);
	p->di = NULL;
}

static void periodic_set(struct di_periodic *p, double interval, double offset) {
	if (!p->di)
		return;
	ev_periodic_set(&p->pt, offset, interval, NULL);
	ev_periodic_again(p->di->loop, &p->pt);
}

static struct di_object *
di_create_periodic(struct di_evmodule *evm, double interval, double offset) {
	auto ret = di_new_object_with_type(struct di_periodic);
	ret->di = evm->di;
	di_ref_object((void *)ret->di);

	ret->dtor = (void *)periodic_dtor;
	di_method(ret, "set", periodic_set, double, double);
	ev_periodic_init(&ret->pt, di_periodic_callback, offset, interval, NULL);
	ev_periodic_start(ret->di->loop, &ret->pt);

	ret->d = di_listen_to_destroyed((void *)evm->di, trivial_destroyed_handler, (void *)ret);

	return (void *)ret;
}

void di_init_event(struct deai *di) {
	auto em = di_new_module_with_type(struct di_evmodule);
	em->di = di;

	di_method(em, "fdevent", di_create_ioev, int, int);
	di_method(em, "timer", di_create_timer, unsigned int);
	di_method(em, "periodic", di_create_periodic, double, double);

	di_register_module(di, "event", (void *)em);
	di_unref_object((void *)em);
}
