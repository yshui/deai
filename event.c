/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai/builtins/event.h>
#include <deai/deai.h>
#include <deai/helper.h>

#include <ev.h>
#include <unistd.h>

#include "di_internal.h"
#include "event.h"
#include "utils.h"

struct di_ioev {
	struct di_object_internal;
	ev_io evh;
	bool running;
};

struct di_timer {
	struct di_object_internal;
	ev_timer evt;
};

struct di_periodic {
	struct di_object_internal;
	ev_periodic pt;
};

static void di_ioev_callback(EV_P_ ev_io *w, int revents) {
	auto ev = container_of(w, struct di_ioev, evh);
	// Keep ev alive during emission
	di_object_with_cleanup unused obj = di_ref_object((struct di_object *)ev);
	int dt = 0;
	if (revents & EV_READ) {
		di_emit(ev, "read");
		dt |= IOEV_READ;
	}
	if (revents & EV_WRITE) {
		di_emit(ev, "write");
		dt |= IOEV_WRITE;
	}
	di_emit(ev, "io", dt);
}

static void di_timer_callback(EV_P_ ev_timer *t, int revents) {
	auto d = container_of(t, struct di_timer, evt);
	// Keep timer alive during emission
	di_object_with_cleanup unused obj = di_ref_object((struct di_object *)d);

	double now = ev_now(EV_A);
	di_object_with_cleanup di_obj = di_object_get_deai_strong((struct di_object *)d);
	DI_CHECK(di_obj);

	auto di = (struct deai *)di_obj;
	ev_timer_stop(di->loop, t);
	di_emit(d, "elapsed", now);

	// This object won't generate further event until the user calls `again`
	// So drop the strong __deai reference
	di_object_downgrade_deai((struct di_object *)d);
}

static void di_periodic_callback(EV_P_ ev_periodic *w, int revents) {
	auto p = container_of(w, struct di_periodic, pt);
	// Keep timer alive during emission
	di_object_with_cleanup unused obj = di_ref_object((struct di_object *)p);

	double now = ev_now(EV_A);
	di_emit(p, "triggered", now);
}

static void di_start_ioev(struct di_object *obj) {
	struct di_ioev *ev = (void *)obj;
	if (ev->running) {
		return;
	}

	auto di_obj = di_object_get_deai_weak(obj);
	if (di_obj == NULL) {
		// deai is shutting down
		return;
	}

	auto di = (struct deai *)di_obj;
	ev_io_start(di->loop, &ev->evh);
	ev->running = true;
	di_object_upgrade_deai(obj);
}

static void di_stop_ioev(struct di_object *obj) {
	struct di_ioev *ev = (void *)obj;
	if (!ev->running) {
		return;
	}

	di_object_with_cleanup di_obj = di_object_get_deai_strong(obj);
	if (di_obj == NULL) {
		return;
	}

	auto di = (struct deai *)di_obj;
	ev_io_stop(di->loop, &ev->evh);
	ev->running = false;

	// This object won't generate further event until the user calls `start`
	// So drop the strong __deai reference
	di_object_downgrade_deai(obj);
}

static void di_toggle_ioev(struct di_object *obj) {
	struct di_ioev *ev = (void *)obj;
	if (ev->running) {
		di_stop_ioev(obj);
	} else {
		di_start_ioev(obj);
	}
}

static void di_modify_ioev(struct di_object *obj, int events) {
	auto ioev = (struct di_ioev *)obj;
	if (events == 0) {
		return di_stop_ioev(obj);
	}

	unsigned int flags = 0;
	if (events & IOEV_READ) {
		flags |= EV_READ;
	}
	if (events & IOEV_WRITE) {
		flags |= EV_WRITE;
	}

#ifdef ev_io_modify
	ev_io_modify(&ioev->evh, flags);
#else
	ev_io_set(&ioev->evh, ioev->evh.fd, flags);
#endif
}

static struct di_object *di_create_ioev(struct di_object *obj, int fd, int t) {
	struct di_module *em = (void *)obj;
	auto ret = di_new_object_with_type(struct di_ioev);
	di_set_type((void *)ret, "deai.builtin.event:IoEv");

	auto di_obj = di_module_get_deai(em);
	if (di_obj == NULL) {
		return di_new_error("deai is shutting down...");
	}

	unsigned int flags = 0;
	if (t & IOEV_READ) {
		flags |= EV_READ;
	}
	if (t & IOEV_WRITE) {
		flags |= EV_WRITE;
	}

	ev_io_init(&ret->evh, di_ioev_callback, fd, flags);
	{
		auto di = (struct deai *)di_obj;
		ev_io_start(di->loop, &ret->evh);
	}

	// Started ioev has strong ref to ddi
	// Stopped has weak ref
	di_member(ret, DEAI_MEMBER_NAME_RAW, di_obj);

	di_method(ret, "start", di_start_ioev);
	di_method(ret, "stop", di_stop_ioev);
	di_method(ret, "toggle", di_toggle_ioev);
	di_method(ret, "modify", di_modify_ioev, int);
	di_method(ret, "close", di_finalize_object);

	ret->dtor = di_stop_ioev;
	ret->running = true;
	return (void *)ret;
}

static void di_timer_stop(struct di_object *obj) {
	struct di_timer *ev = (void *)obj;
	di_object_with_cleanup di_obj = di_object_get_deai_strong(obj);
	if (di_obj == NULL) {
		// deai is shutting down
		return;
	}

	auto di = (struct deai *)di_obj;
	ev_timer_stop(di->loop, &ev->evt);
	di_object_downgrade_deai(obj);
}

static void di_timer_again(struct di_timer *obj) {
	auto di_obj = di_object_get_deai_weak((struct di_object *)obj);
	if (di_obj == NULL) {
		// deai is shutting down
		return;
	}

	auto di = (struct deai *)di_obj;
	ev_timer_again(di->loop, &obj->evt);
	di_object_upgrade_deai((struct di_object *)obj);
}

static void di_timer_set(struct di_timer *obj, double t) {
	di_timer_stop((struct di_object *)obj);
	obj->evt.repeat = t;
	di_timer_again(obj);
}

static struct di_object *di_create_timer(struct di_object *obj, double timeout) {
	struct di_module *em = (void *)obj;
	auto ret = di_new_object_with_type(struct di_timer);
	di_set_type((void *)ret, "deai.builtin.event:Timer");
	auto di_obj = di_module_get_deai(em);
	if (di_obj == NULL) {
		return di_new_error("deai is shutting down...");
	}

	ret->dtor = di_timer_stop;
	di_method(ret, "again", di_timer_again);
	di_method(ret, "stop", di_timer_stop);

	// Set the timeout and restart the timer
	di_method(ret, "__set_timeout", di_timer_set, double);

	ev_init(&ret->evt, di_timer_callback);
	ret->evt.repeat = timeout;

	auto di = (struct deai *)di_obj;
	ev_timer_again(di->loop, &ret->evt);

	// Started timers have strong references to di
	// Stopped ones have weak ones
	di_member(ret, DEAI_MEMBER_NAME_RAW, di_obj);
	return (struct di_object *)ret;
}

static void periodic_dtor(struct di_periodic *p) {
	di_object_with_cleanup di_obj = di_object_get_deai_strong((struct di_object *)p);
	auto di = (struct deai *)di_obj;
	ev_periodic_stop(di->loop, &p->pt);
}

static void periodic_set(struct di_periodic *p, double interval, double offset) {
	di_object_with_cleanup di_obj = di_object_get_deai_strong((struct di_object *)p);
	DI_CHECK(di_obj != NULL);
	ev_periodic_set(&p->pt, offset, interval, NULL);

	auto di = (struct deai *)di_obj;
	ev_periodic_again(di->loop, &p->pt);
}

static struct di_object *
di_create_periodic(struct di_module *evm, double interval, double offset) {
	auto ret = di_new_object_with_type(struct di_periodic);
	di_set_type((void *)ret, "deai.builtin.event:Periodic");
	auto di_obj = di_module_get_deai(evm);

	ret->dtor = (void *)periodic_dtor;
	di_method(ret, "set", periodic_set, double, double);
	ev_periodic_init(&ret->pt, di_periodic_callback, offset, interval, NULL);

	auto di = (struct deai *)di_obj;
	ev_periodic_start(di->loop, &ret->pt);

	di_member(ret, DEAI_MEMBER_NAME_RAW, di_obj);
	return (void *)ret;
}

struct di_prepare {
	ev_prepare;
	struct di_module *evm;
};

static void di_prepare(EV_P_ ev_prepare *w, int revents) {
	struct di_prepare *dep = (void *)w;
	// Keep event module alive during emission
	di_object_with_cleanup unused obj = di_ref_object((struct di_object *)dep->evm);
	di_emit(dep->evm, "prepare");
}

void di_init_event(struct deai *di) {
	auto em = di_new_module(di);

	di_method(em, "fdevent", di_create_ioev, int, int);
	di_method(em, "timer", di_create_timer, double);
	di_method(em, "periodic", di_create_periodic, double, double);

	auto dep = tmalloc(struct di_prepare, 1);
	dep->evm = em;
	ev_prepare_init(dep, di_prepare);
	ev_prepare_start(di->loop, (ev_prepare *)dep);

	di_register_module(di, di_string_borrow("event"), &em);
}
