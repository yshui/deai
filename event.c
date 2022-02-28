/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai/builtins/event.h>
#include <deai/builtins/log.h>
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
	uint64_t root_handle;
};

struct di_periodic {
	struct di_object_internal;
	ev_periodic pt;
};

/// SIGNAL: deai.builtin.event:IoEv.read() File descriptor became readable
///
/// SIGNAL: deai.builtin.event:IoEv.write() File descriptor became writable
///
/// SIGNAL: deai.builtin.event:IoEv.io(flag: :integer) File descriptor became either
/// readable or writable
static void di_ioev_callback(EV_P_ ev_io *w, int revents) {
	auto ev = container_of(w, struct di_ioev, evh);
	// Keep ev alive during emission
	di_object_with_cleanup unused obj = di_ref_object((struct di_object *)ev);
	if (revents & EV_READ) {
		di_emit(ev, "read");
	}
	if (revents & EV_WRITE) {
		di_emit(ev, "write");
	}
}

/// SIGNAL: deai.builtin.event:Timer.elapsed(now: :float) Timeout was reached
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

/// SIGNAL: deai.builtin.event:Periodic.triggered(now: :float) Timeout was reached
static void di_periodic_callback(EV_P_ ev_periodic *w, int revents) {
	auto p = container_of(w, struct di_periodic, pt);
	// Keep timer alive during emission
	di_object_with_cleanup unused obj = di_ref_object((struct di_object *)p);

	double now = ev_now(EV_A);
	di_emit(p, "triggered", now);
}

/// Start the event source
///
/// EXPORT: deai.builtin.event:IoEv.start(), :void
static void di_start_ioev(struct di_ioev *ev) {
	if (ev->running) {
		return;
	}

	di_object_with_cleanup di_obj = di_object_get_deai_weak((void *)ev);
	if (di_obj == NULL) {
		// deai is shutting down
		return;
	}

	auto di = (struct deai *)di_obj;
	ev_io_start(di->loop, &ev->evh);
	ev->running = true;

	// Keep the mainloop alive while we are running
	di_object_upgrade_deai((void *)ev);

	// Add event source to roots when it's running
	auto roots = di_get_roots();
	di_string_with_cleanup root_key = di_string_printf("fdevent_for_%d", ev->evh.fd);
	DI_CHECK_OK(di_call(roots, "add", root_key, (struct di_object *)ev));
}

/// Stop the event source
///
/// EXPORT: deai.builtin.event:IoEv.stop(), :void
static void di_stop_ioev(struct di_ioev *ev) {
	if (!ev->running) {
		return;
	}

	di_object_with_cleanup di_obj = di_object_get_deai_strong((void *)ev);
	if (di_obj == NULL) {
		return;
	}

	auto di = (struct deai *)di_obj;
	ev_io_stop(di->loop, &ev->evh);
	ev->running = false;

	// Stop referencing the mainloop since we are not running
	di_object_downgrade_deai((void *)ev);

	auto roots = di_get_roots();
	di_string_with_cleanup root_key = di_string_printf("fdevent_for_%d", ev->evh.fd);

	// Ignore error here, if someone called di:exit, we would've been removed already.
	di_call(roots, "remove", root_key);
}

/// Change monitored file descriptor events
///
/// EXPORT: deai.builtin.event:IoEv.modify(flag: :integer), :void
static void di_modify_ioev(struct di_ioev *ioev, unsigned int flags) {
#ifdef ev_io_modify
	ev_io_modify(&ioev->evh, flags);
#else
	ev_io_set(&ioev->evh, ioev->evh.fd, flags);
#endif
	if (!ioev->running && flags != 0) {
		di_start_ioev(ioev);
	} else if (ioev->running && flags == 0) {
		di_stop_ioev(ioev);
	}
}

static void di_enable_read(struct di_object *obj, struct di_object *sig) {
	if (di_member_clone(obj, "__signal_read", sig) != 0) {
		return;
	}

	auto ioev = (struct di_ioev *)obj;
	unsigned int flags = ioev->evh.events;
	flags |= EV_READ;
	di_modify_ioev(ioev, flags);
}

static void di_enable_write(struct di_object *obj, struct di_object *sig) {
	if (di_member_clone(obj, "__signal_write", sig) != 0) {
		return;
	}

	auto ioev = (struct di_ioev *)obj;
	unsigned int flags = ioev->evh.events;
	flags |= EV_WRITE;
	di_modify_ioev(ioev, flags);
}

static void di_disable_read(struct di_object *obj) {
	if (di_remove_member_raw(obj, di_string_borrow("__signal_read")) != 0) {
		return;
	}

	auto ioev = (struct di_ioev *)obj;
	unsigned int flags = ioev->evh.events & (~EV_READ);
	di_modify_ioev(ioev, flags);
}

static void di_disable_write(struct di_object *obj) {
	if (di_remove_member_raw(obj, di_string_borrow("__signal_write")) != 0) {
		return;
	}

	auto ioev = (struct di_ioev *)obj;
	unsigned int flags = ioev->evh.events & (~EV_WRITE);
	di_modify_ioev(ioev, flags);
}

static void di_ioev_dtor(struct di_object *obj) {
	// Normally the ev_io won't be running, but if someone removed us from the roots,
	// e.g. by calling di:exit(), then ev_io could be running.
	// Removing the signal objects should be enough to stop it.
	di_remove_member(obj, di_string_borrow_literal("__signal_read"));
	di_remove_member(obj, di_string_borrow_literal("__signal_write"));
}

/// File descriptor events
///
/// EXPORT: event.fdevent(fd: :integer, flag: :integer), deai.builtin.event:IoEv
///
/// Arguments:
///
/// - flag bit mask of which events to monitor. bit 0 for readability, bit 1 for
///        writability.
///
/// Wait for a file descriptor to be readable/writable.
static struct di_object *di_create_ioev(struct di_object *obj, int fd) {
	struct di_module *em = (void *)obj;
	auto ret = di_new_object_with_type(struct di_ioev);
	di_set_type((void *)ret, "deai.builtin.event:IoEv");
	di_set_object_dtor((void *)ret, di_ioev_dtor);

	di_object_with_cleanup di_obj = di_module_get_deai(em);
	if (di_obj == NULL) {
		return di_new_error("deai is shutting down...");
	}

	ev_io_init(&ret->evh, di_ioev_callback, fd, 0);
	ret->running = false;

	// Started ioev has strong ref to ddi
	// Stopped has weak ref
	auto weak_di = di_weakly_ref_object(di_obj);
	di_member(ret, DEAI_MEMBER_NAME_RAW, weak_di);

	di_signal_setter_deleter(ret, "read", di_enable_read, di_disable_read);
	di_signal_setter_deleter(ret, "write", di_enable_write, di_disable_write);
	return (void *)ret;
}

/// Stop the timer
///
/// EXPORT: deai.builtin.event:Timer.stop(), :void
static void di_timer_stop(struct di_object *obj) {
	struct di_timer *ev = (void *)obj;
	di_object_with_cleanup di_obj = di_object_get_deai_strong(obj);
	if (di_obj == NULL) {
		// this means the timer was already stopped, so it doesn't hold a strong
		// deai object reference
		DI_ASSERT(!ev_is_active(&ev->evt));
		return;
	}

	auto di = (struct deai *)di_obj;
	ev_timer_stop(di->loop, &ev->evt);
	di_object_downgrade_deai(obj);
}

/// Re-arm the timer
///
/// EXPORT: deai.builtin.event:Timer.again(), :void
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

/// Timer timeout
///
/// EXPORT: deai.builtin.event:Timer.timeout, :float
///
/// Write-only property to update the timer's timeout. Timer will be re-armed after the
/// update.
static void di_timer_set(struct di_timer *obj, double t) {
	di_timer_stop((struct di_object *)obj);
	obj->evt.repeat = t;
	di_timer_again(obj);
}

static void di_timer_add_signal(struct di_object *o, struct di_object *sig) {
	if (di_member_clone(o, "__signal_elapsed", sig) != 0) {
		return;
	}

	auto t = (struct di_timer *)o;
	di_object_upgrade_deai(o);

	// Add ourselve to GC root
	auto roots = di_get_roots();
	DI_CHECK_OK(di_callr(roots, "__add_anonymous", t->root_handle, o));
}

static void di_timer_delete_signal(struct di_object *o) {
	if (di_remove_member_raw(o, di_string_borrow("__signal_elapsed")) != 0) {
		return;
	}

	auto t = (struct di_timer *)o;
	auto roots = di_get_roots();
	DI_CHECK_OK(di_call(roots, "__remove_anonymous", t->root_handle));

	di_object_downgrade_deai(o);
}

/// Timer events
///
/// EXPORT: event.timer(timeout: :float), deai.builtin.event:Timer
///
/// Arguments:
///
/// - timeout timeout in seconds
///
/// Create a timer that emits a signal after timeout is reached.
static struct di_object *di_create_timer(struct di_object *obj, double timeout) {
	struct di_module *em = (void *)obj;
	auto ret = di_new_object_with_type(struct di_timer);
	di_set_type((void *)ret, "deai.builtin.event:Timer");
	di_object_with_cleanup di_obj = di_module_get_deai(em);
	if (di_obj == NULL) {
		return di_new_error("deai is shutting down...");
	}

	ret->dtor = di_timer_stop;
	di_method(ret, "again", di_timer_again);
	di_method(ret, "stop", di_timer_stop);

	// Set the timeout and restart the timer
	di_method(ret, "__set_timeout", di_timer_set, double);
	di_signal_setter_deleter(ret, "elapsed", di_timer_add_signal, di_timer_delete_signal);

	ev_init(&ret->evt, di_timer_callback);
	ret->evt.repeat = timeout;

	auto di = (struct deai *)di_obj;
	ev_timer_again(di->loop, &ret->evt);

	// Started timers have strong references to di
	// Stopped ones have weak ones
	auto weak_di = di_weakly_ref_object(di_obj);
	di_member(ret, DEAI_MEMBER_NAME_RAW, weak_di);
	return (struct di_object *)ret;
}

static void periodic_dtor(struct di_periodic *p) {
	di_object_with_cleanup di_obj = di_object_get_deai_strong((struct di_object *)p);
	auto di = (struct deai *)di_obj;
	ev_periodic_stop(di->loop, &p->pt);
}

/// Update timer interval and offset
///
/// EXPORT: deai.builtin.event:Periodic.set(interval: :float, offset: :float), :void
///
/// Timer will be reset after update.
static void periodic_set(struct di_periodic *p, double interval, double offset) {
	di_object_with_cleanup di_obj = di_object_get_deai_strong((struct di_object *)p);
	DI_CHECK(di_obj != NULL);
	ev_periodic_set(&p->pt, offset, interval, NULL);

	auto di = (struct deai *)di_obj;
	ev_periodic_again(di->loop, &p->pt);
}

/// Periodic timer event
///
/// EXPORT: event.periodic(interval: :float, offset: :float), deai.builtin.event:Periodic
///
/// A timer that first fire after :code:`offset` seconds, then every :code:`interval`
/// seconds.
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
	bool has_cycle;
	if (di_mark_and_sweep(&has_cycle) || has_cycle) {
		di_log_va(log_module, DI_LOG_DEBUG, "Reference bug detected\n");
		di_dump_objects();
#ifdef UNITTESTS
		abort();
#endif
	}

	struct di_prepare *dep = (void *)w;
	// Keep event module alive during emission
	di_object_with_cleanup unused obj = di_ref_object((struct di_object *)dep->evm);
	di_emit(dep->evm, "prepare");
}

/// Core events
///
/// EXPORT: event, deai:module
///
/// Fundament event sources exposed by deai. This is the building blocks of other event
/// sources.
void di_init_event(struct deai *di) {
	auto em = di_new_module(di);

	di_method(em, "fdevent", di_create_ioev, int);
	di_method(em, "timer", di_create_timer, double);
	di_method(em, "periodic", di_create_periodic, double, double);

	auto dep = tmalloc(struct di_prepare, 1);
	dep->evm = em;
	ev_prepare_init(dep, di_prepare);
	ev_prepare_start(di->loop, (ev_prepare *)dep);

	di_register_module(di, di_string_borrow("event"), &em);
}
