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
	di_object_internal;
	ev_io evh;
	bool running;
};

struct di_timer {
	di_object_internal;
	ev_timer evt;
	double at;
	bool spent;
};

struct di_periodic {
	di_object_internal;
	ev_periodic pt;
	bool running;
};

typedef struct di_event_module {
	di_object_internal;
	ev_idle idlew;
} di_event_module;

/// SIGNAL: deai.builtin.event:IoEv.read() File descriptor became readable
///
/// SIGNAL: deai.builtin.event:IoEv.write() File descriptor became writable
///
/// SIGNAL: deai.builtin.event:IoEv.io(flag: :integer) File descriptor became either
/// readable or writable
static void di_ioev_callback(EV_P_ ev_io *w, int revents) {
	auto ev = container_of(w, struct di_ioev, evh);
	if (revents & EV_READ) {
		di_emit(ev, "read");
	}
	if (revents & EV_WRITE) {
		di_emit(ev, "write");
	}
}

/// SIGNAL: deai.builtin.event:Periodic.triggered(now: :float) Timeout was reached
static void di_periodic_callback(EV_P_ ev_periodic *w, int revents) {
	auto p = container_of(w, struct di_periodic, pt);
	double now = ev_now(EV_A);
	di_emit(p, "triggered", now);
}

/// Start the event source
///
/// EXPORT: deai.builtin.event:IoEv.start(): :void
static void di_start_ioev(struct di_ioev *ev) {
	if (ev->running) {
		return;
	}

	di_object *di_obj = di_object_borrow_deai((void *)ev);
	if (di_obj == NULL) {
		// deai is shutting down
		return;
	}

	auto di = (struct deai *)di_obj;
	ev_io_start(di->loop, &ev->evh);

	// Add event source to roots when it's running
	auto roots = di_get_roots();
	DI_CHECK_OK(di_callr(roots, "add_anonymous", ev->running, (di_object *)ev));
	DI_CHECK(ev->running);
}

/// Stop the event source
///
/// EXPORT: deai.builtin.event:IoEv.stop(): :void
static void di_stop_ioev(struct di_ioev *ev) {
	if (!ev->running) {
		return;
	}

	{
		di_object *di_obj = di_object_borrow_deai((void *)ev);
		if (di_obj == NULL) {
			return;
		}

		auto di = (struct deai *)di_obj;
		ev_io_stop(di->loop, &ev->evh);
		ev->running = false;
	}

	auto roots = di_get_roots();
	// Ignore error here, if someone called di:exit, we would've been removed already.
	di_call(roots, "remove_anonymous", (di_object *)ev);
}

/// Change monitored file descriptor events
///
/// EXPORT: deai.builtin.event:IoEv.modify(flag: :integer): :void
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

static void di_enable_read(di_object *obj, di_object *sig) {
	if (di_member_clone(obj, "__signal_read", sig) != 0) {
		return;
	}

	auto ioev = (struct di_ioev *)obj;
	unsigned int flags = ioev->evh.events;
	flags |= EV_READ;
	di_modify_ioev(ioev, flags);
}

static void di_enable_write(di_object *obj, di_object *sig) {
	if (di_member_clone(obj, "__signal_write", sig) != 0) {
		return;
	}

	auto ioev = (struct di_ioev *)obj;
	unsigned int flags = ioev->evh.events;
	flags |= EV_WRITE;
	di_modify_ioev(ioev, flags);
}

static void di_disable_read(di_object *obj) {
	if (di_delete_member_raw(obj, di_string_borrow("__signal_read")) != 0) {
		return;
	}

	auto ioev = (struct di_ioev *)obj;
	unsigned int flags = ioev->evh.events & (~EV_READ);
	di_modify_ioev(ioev, flags);
}

static void di_disable_write(di_object *obj) {
	if (di_delete_member_raw(obj, di_string_borrow("__signal_write")) != 0) {
		return;
	}

	auto ioev = (struct di_ioev *)obj;
	unsigned int flags = ioev->evh.events & (~EV_WRITE);
	di_modify_ioev(ioev, flags);
}

static void di_ioev_dtor(di_object *obj) {
	// Normally the ev_io won't be running, but if someone removed us from the roots,
	// e.g. by calling di:exit(), then ev_io could be running.
	// Removing the signal objects, the deleter should stop it.
	di_delete_member(obj, di_string_borrow_literal("__signal_read"));
	di_delete_member(obj, di_string_borrow_literal("__signal_write"));
}

/// File descriptor events
///
/// EXPORT: event.fdevent(fd: :integer, flag: :integer): deai.builtin.event:IoEv
///
/// Arguments:
///
/// - flag bit mask of which events to monitor. bit 0 for readability, bit 1 for
///        writability.
///
/// Wait for a file descriptor to be readable/writable.
static di_object *di_create_ioev(di_object *obj, int fd) {
	struct di_module *em = (void *)obj;
	auto ret = di_new_object_with_type(struct di_ioev);
	di_set_type((void *)ret, "deai.builtin.event:IoEv");
	di_set_object_dtor((void *)ret, di_ioev_dtor);

	auto di_obj = di_module_get_deai(em);
	if (di_obj == NULL) {
		return di_new_error("deai is shutting down...");
	}

	ev_io_init(&ret->evh, di_ioev_callback, fd, 0);
	ret->running = false;

	// Started ioev has strong ref to ddi
	// Stopped has weak ref
	di_member(ret, DEAI_MEMBER_NAME_RAW, di_obj);

	di_signal_setter_deleter(ret, "read", di_enable_read, di_disable_read);
	di_signal_setter_deleter(ret, "write", di_enable_write, di_disable_write);
	return (void *)ret;
}

static void di_timer_delete_signal(di_object *o) {
	if (di_delete_member_raw(o, di_string_borrow("__signal_elapsed")) != 0) {
		return;
	}

	auto t = (struct di_timer *)o;
	auto roots = di_get_roots();

	// Ignore error because roots might have been removed by di:exit
	di_call(roots, "remove_anonymous", o);

	di_object *di_obj = di_object_borrow_deai(o);
	auto di = (struct deai *)di_obj;
	ev_timer_stop(di->loop, &t->evt);
}

static void di_timer_add_signal(di_object *o, di_object *sig) {
	auto t = (struct di_timer *)o;
	if (t->spent) {
		return;
	}

	di_object *di_obj = di_object_borrow_deai(o);
	if (di_obj == NULL) {
		return;
	}

	if (di_member_clone(o, "__signal_elapsed", sig) != 0) {
		return;
	}

	// Add ourselve to GC root
	auto roots = di_get_roots();
	bool added = false;
	if (di_callr(roots, "add_anonymous", added, o) != 0) {
		// Could happen if di:exit is called
		di_timer_delete_signal(o);
		return;
	}
	DI_CHECK(added);
	auto di = (struct deai *)di_obj;

	// Recalculate timeout
	double after = t->at - ev_now(di->loop);
	ev_timer_set(&t->evt, after, 0.0);
	ev_timer_start(di->loop, &t->evt);
}

/// SIGNAL: deai.builtin.event:Timer.elapsed(now: :float) Timeout was reached
static void di_timer_callback(EV_P_ ev_timer *t, int revents) {
	auto d = container_of(t, struct di_timer, evt);
	// Keep timer alive because we will still be using it after emission.
	scoped_di_object unused *obj = di_ref_object((di_object *)d);

	double now = ev_now(EV_A);
	di_object *di_obj = di_object_borrow_deai((di_object *)d);
	DI_CHECK(di_obj);

	auto di = (struct deai *)di_obj;
	ev_timer_stop(di->loop, t);
	d->spent = true;
	di_emit(d, "elapsed", now);

	di_timer_delete_signal((void *)d);
}

/// Timer events
///
/// EXPORT: event.timer(timeout: :float): deai.builtin.event:Timer
///
/// Arguments:
///
/// - timeout timeout in seconds
///
/// Create a timer that emits a signal after timeout is reached. Note that signals will
/// only be emitted if listeners exist. If no listeners existed during the timeout window,
/// the singal will be emitted when the first listener is attached.
static di_object *di_create_timer(di_object *obj, double timeout) {
	struct di_module *em = (void *)obj;
	auto ret = di_new_object_with_type(struct di_timer);
	di_set_type((void *)ret, "deai.builtin.event:Timer");
	auto di_obj = di_module_get_deai(em);
	if (di_obj == NULL) {
		return di_new_error("deai is shutting down...");
	}

	auto di = (struct deai *)di_obj;

	ret->spent = false;
	ret->dtor = di_timer_delete_signal;
	di_signal_setter_deleter(ret, "elapsed", di_timer_add_signal, di_timer_delete_signal);

	ret->at = ev_now(di->loop) + timeout;
	ev_timer_init(&ret->evt, di_timer_callback, timeout, 0.0);

	// Started timers have strong references to di
	// Stopped ones have weak ones
	di_member(ret, DEAI_MEMBER_NAME_RAW, di_obj);
	return (di_object *)ret;
}

/// Update timer interval and offset
///
/// EXPORT: deai.builtin.event:Periodic.set(interval: :float, offset: :float): :void
///
/// Timer will be reset after update.
static void periodic_set(struct di_periodic *p, double interval, double offset) {
	auto di_obj = di_object_borrow_deai((di_object *)p);
	DI_CHECK(di_obj != NULL);
	ev_periodic_set(&p->pt, offset, interval, NULL);

	auto di = (struct deai *)di_obj;
	ev_periodic_again(di->loop, &p->pt);
}

static void di_periodic_signal_setter(di_object *o, di_object *sig) {
	if (di_member_clone(o, di_signal_member_of("triggered"), sig) != 0) {
		return;
	}
	auto roots = di_get_roots();
	bool added = false;
	DI_CHECK_OK(di_callr(roots, "add_anonymous", added, o));
	DI_CHECK(added);
}

static void di_periodic_signal_deleter(di_object *o) {
	if (di_delete_member_raw(
	        o, di_string_borrow_literal(di_signal_member_of("triggered"))) != 0) {
		// Could happen this function is called as destructor
		return;
	}

	auto roots = di_get_roots();
	di_call(roots, "remove_anonymous", o);
}

/// Periodic timer event
///
/// EXPORT: event.periodic(interval: :float, offset: :float): deai.builtin.event:Periodic
///
/// A timer that first fire after :code:`offset` seconds, then every :code:`interval`
/// seconds.
static di_object *di_create_periodic(struct di_module *evm, double interval, double offset) {
	auto ret = di_new_object_with_type(struct di_periodic);
	di_set_type((void *)ret, "deai.builtin.event:Periodic");
	auto di_obj = di_module_get_deai(evm);

	ret->dtor = (void *)di_periodic_signal_deleter;
	di_method(ret, "set", periodic_set, double, double);
	ev_periodic_init(&ret->pt, di_periodic_callback, offset, interval, NULL);
	di_signal_setter_deleter(ret, "triggered", di_periodic_signal_setter,
	                         di_periodic_signal_deleter);

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
	if (di_mark_and_sweep(&has_cycle)) {
		di_log_va(log_module, DI_LOG_DEBUG, "Reference bug detected\n");
		di_dump_objects();
#ifdef UNITTESTS
		abort();
#endif
	}

	struct di_prepare *dep = (void *)w;
	// Event module could be freed by garbage collector (because here we don't
	// increment the reference count). Use a weak reference to detect when it's freed.
	scoped_di_weak_object *weak_eventm = di_weakly_ref_object((void *)dep->evm);
	di_collect_garbage();

	// Keep event module alive during emission
	scoped_di_object *obj = di_upgrade_weak_ref(weak_eventm);
	if (obj) {
		di_emit(obj, "prepare");
	}
}

/// A pending value
///
/// TYPE: deai:Promise
///
/// This encapsulates a pending value. Once this value become available, a "resolved"
/// signal will be emitted with the value. Each promise should resolve only once ever.
struct di_promise {
	di_object;
};

di_object *di_promise_then(di_object *promise, di_object *handler);
void di_resolve_promise(struct di_promise *promise, struct di_variant var);
static void di_promise_then_impl(struct di_promise *promise,
                                 struct di_promise *then_promise, di_object *handler);

static int di_promise_dispatch(di_promise *promise) {
	di_variant resolved;
	uint64_t nhandlers;
	DI_CHECK_OK(di_get(promise, "___resolved", resolved));
	DI_CHECK_OK(di_get(promise, "___n_handlers", nhandlers));

	// Reset number of handlers
	di_setx((void *)promise, di_string_borrow("___n_handlers"), DI_TYPE_UINT,
	        (uint64_t[]){0});
	auto handlers = tmalloc(di_object *, nhandlers);
	auto then_promises = tmalloc(di_object *, nhandlers);

	// Move out all the handlers and promises in case they got overwritten by the
	// handlers.
	for (uint64_t i = 0; i < nhandlers; i++) {
		scoped_di_string str = DI_STRING_INIT;
		di_variant var = DI_VARIANT_INIT;
		str = di_string_printf("___then_handler_%lu", i);
		handlers[i] = NULL;
		if (di_remove_member_raw((void *)promise, str, &var) == 0) {
			di_type_conversion(DI_TYPE_VARIANT, (di_value *)&var,
			                   DI_TYPE_OBJECT, (di_value *)&handlers[i], false);
		}
		di_free_string(str);

		str = di_string_printf("___then_promise_%lu", i);
		DI_CHECK_OK(di_remove_member_raw((void *)promise, str, &var));
		di_type_conversion(DI_TYPE_VARIANT, (di_value *)&var, DI_TYPE_OBJECT,
		                   (di_value *)&then_promises[i], false);
	}

	for (uint64_t i = 0; i < nhandlers; i++) {
		di_value return_value;
		di_type return_type;
		int ret = 0;
		if (handlers[i]) {
			ret = di_call_object(handlers[i], &return_type, &return_value,
			                     DI_TYPE_VARIANT, resolved, DI_LAST_TYPE);
			di_unref_object(handlers[i]);
			handlers[i] = NULL;
		} else {
			// If there is no handler, we simply copy the value
			di_copy_value(resolved.type, &return_value, resolved.value);
			return_type = resolved.type;
		}
		if (ret != 0) {
			return_value.object = di_new_error("Failed to call function in "
			                                   "promise then");
			return_type = DI_TYPE_OBJECT;
		}
		if (return_type == DI_TYPE_OBJECT &&
		    strcmp(di_get_type(return_value.object), "deai:Promise") == 0) {
			di_promise_then_impl((void *)return_value.object,
			                     (void *)then_promises[i], NULL);
		} else {
			di_resolve_promise(
			    (void *)then_promises[i],
			    (struct di_variant){.type = return_type, .value = &return_value});
		}
		di_unref_object((void *)then_promises[i]);
		di_free_value(return_type, &return_value);
	}
	free(handlers);
	free(then_promises);
	di_free_value(DI_TYPE_VARIANT, (void *)&resolved);
	di_delete_member_raw((void *)promise, di_string_borrow("is_pending"));
	return 0;
}

/// Create a new promise object
///
/// EXPORT: event.new_promise(): deai:Promise
di_object *di_new_promise(di_object *event_module) {
	struct di_promise *ret = di_new_object_with_type(struct di_promise);
	auto weak_event = di_weakly_ref_object(event_module);
	di_set_type((void *)ret, "deai:Promise");
	di_member(ret, "___weak_event_module", weak_event);
	di_setx((void *)ret, di_string_borrow("___n_handlers"), DI_TYPE_UINT, (uint64_t[]){0});
	di_method(ret, "then", di_promise_then, di_object *);
	// "then" is a keyword in lua
	di_method(ret, "then_", di_promise_then, di_object *);
	di_method(ret, "resolve", di_resolve_promise, struct di_variant);
	return (void *)ret;
}

static void di_promise_start_dispatch(struct di_promise *promise) {
	if (di_lookup((void *)promise, di_string_borrow("is_pending"))) {
		// Already started dispatch
		return;
	}

	scoped_di_weak_object *weak_event = NULL;
	DI_CHECK_OK(di_get(promise, "___weak_event_module", weak_event));
	scoped_di_object *event_module = di_upgrade_weak_ref(weak_event);
	if (event_module == NULL) {
		return;
	}

	di_object *di_obj = di_object_borrow_deai(event_module);
	if (di_obj == NULL) {
		// deai is shutting down
		return;
	}
	auto di = (struct deai *)di_obj;
	ev_idle_start(di->loop, &((di_event_module *)event_module)->idlew);

	int pending_count = 0;
	DI_CHECK_OK(di_get(event_module, "pending_count", pending_count));
	scoped_di_string key = di_string_printf("pending_promise_%d", pending_count);

	di_rawsetx((void *)event_module, di_string_borrow_literal("pending_count"),
	           DI_TYPE_NINT, (int[]){pending_count + 1});

	if (pending_count == 0) {
		// Add event_module to roots
		auto roots = di_get_roots();
		di_call(roots, "add_anonymous", event_module);
	}

	di_add_member_clone(event_module, key, DI_TYPE_OBJECT, &promise);
	di_member_clone(promise, "is_pending", true);
}

static void di_promise_then_impl(struct di_promise *promise,
                                 struct di_promise *then_promise, di_object *handler) {
	uint64_t nhandlers;
	DI_CHECK_OK(di_get(promise, "___n_handlers", nhandlers));
	char *buf;
	if (handler) {
		asprintf(&buf, "___then_handler_%lu", nhandlers);
		di_member_clone(promise, buf, handler);
		free(buf);
	}
	asprintf(&buf, "___then_promise_%lu", nhandlers);
	di_member_clone(promise, buf, (di_object *)then_promise);
	free(buf);

	nhandlers += 1;
	di_setx((void *)promise, di_string_borrow("___n_handlers"), DI_TYPE_UINT, &nhandlers);

	if (di_lookup((void *)promise, di_string_borrow("___resolved"))) {
		di_promise_start_dispatch(promise);
	}
}

/// Chain computation to a promise
///
/// EXPORT: deai:Promise.then(handler: :object): deai:Promise
///
/// Register a handler to be called after `promise` resolves, the handler will be called
/// with the resolved value as argument.
///
/// Returns a promise that will be resolved after handler returns. If handler returns
/// another promise, then returned promise will be resolved after that promises resolves,
/// otherwise return promise will resolve to the value returned by the handler.
///
/// Note the handler will always be called after being registered, whether the
/// promise returned by this function is freed or not.
///
/// (this function is called "then\_" in lua, since "then" is a keyword)
di_object *di_promise_then(di_object *promise, di_object *handler) {
	scoped_di_weak_object *weak_event = NULL;
	if (di_get(promise, "___weak_event_module", weak_event) != 0) {
		return di_new_error("Event module member not found");
	}
	scoped_di_object *event_module = di_upgrade_weak_ref(weak_event);
	if (event_module == NULL) {
		return di_new_error("deai shutting down?");
	}

	di_object *ret = di_new_promise(event_module);
	di_promise_then_impl((void *)promise, (void *)ret, handler);
	return ret;
}

static void di_promise_join_handler(int index, di_object *storage,
                                    di_object *then_promise, struct di_variant var) {
	scoped_di_string key = di_string_printf("%d", index);
	DI_CHECK_OK(di_add_member_clone(storage, key, DI_TYPE_VARIANT, &var));

	int left;
	DI_CHECK_OK(di_get(storage, "left", left));
	left -= 1;
	DI_CHECK_OK(di_setx(storage, di_string_borrow_literal("left"), DI_TYPE_NINT, &left));

	if (left == 0) {
		int total;
		DI_CHECK_OK(di_get(storage, "total", total));
		scoped_di_tuple results = {.length = total,
		                           .elements = tmalloc(struct di_variant, total)};
		for (int i = 0; i < total; i++) {
			scoped_di_string key = di_string_printf("%d", i);
			di_variant tmp;
			DI_CHECK_OK(di_get2(storage, key, tmp));
			results.elements[i] = tmp;
		}
		di_call(then_promise, "resolve", results);
	}
}

/// Create a promise that resolves when all given promises resolve. Returns a promises
/// that resolves into an array, which stores the results of the promises.
///
/// The promises can resolve in any order.
///
/// EXPORT: event.join_promises(promises: [deai:Promise]): deai:Promise
di_object *di_join_promises(di_object *event_module, di_array promises) {
	if (promises.length > 0 && promises.elem_type != DI_TYPE_OBJECT) {
		return di_new_error("promises must all be objects");
	}
	di_object **arr = promises.arr;
	for (int i = 0; i < promises.length; i++) {
		if (!di_check_type(arr[i], "deai:Promise")) {
			return di_new_error("not all objects are promise");
		}
	}
	auto ret = di_new_promise(event_module);
	if (promises.length == 0) {
		di_array arg = DI_ARRAY_INIT;
		di_call(ret, "resolve", arg);
		return ret;
	}
	scoped_di_object *storage = di_new_object_with_type(di_object);
	int cnt = 0;
	for (int i = 0; i < promises.length; i++) {
		scoped_di_object *handler = (void *)di_make_closure(
		    di_promise_join_handler, (cnt, storage, ret), struct di_variant);
		if (di_call(arr[i], "then", handler) == 0) {
			cnt += 1;
		}
	}
	DI_CHECK_OK(di_setx(storage, di_string_borrow_literal("left"), DI_TYPE_NINT, &cnt));
	DI_CHECK_OK(di_setx(storage, di_string_borrow_literal("total"), DI_TYPE_NINT, &cnt));
	return ret;
}

static void di_any_promise_handler(di_object *then, struct di_variant var) {
	di_call(then, "resolve", var);
}

/// Create a promise that resolves when any of given promises resolve
///
/// EXPORT: event.any_promise(promises: [deai:Promise]): deai:Promise
di_object *di_any_promise(di_object *event_module, di_array promises) {
	if (promises.elem_type != DI_TYPE_OBJECT) {
		return di_new_error("promises must all be objects");
	}
	di_object **arr = promises.arr;
	for (int i = 0; i < promises.length; i++) {
		if (!di_check_type(arr[i], "deai:Promise")) {
			return di_new_error("not all objects are promise");
		}
	}
	auto ret = di_new_promise(event_module);
	for (int i = 0; i < promises.length; i++) {
		scoped_di_object *handler =
		    (void *)di_make_closure(di_any_promise_handler, (ret), struct di_variant);
		di_call(arr[i], "then", handler);
	}
	return ret;
}

void di_resolve_promise(struct di_promise *promise, struct di_variant var) {
	if (di_has_member(promise, "___resolved")) {
		// Already resolved
		return;
	}
	di_member_clone(promise, "___resolved", var);
	di_promise_start_dispatch(promise);
}

void di_idle_cb(EV_P_ ev_idle *w, int revents) {
	ev_idle_stop(EV_A_ w);
	auto eventm = container_of(w, di_event_module, idlew);
	do {
		int pending_count = 0;
		DI_CHECK_OK(di_get(eventm, "pending_count", pending_count));
		if (pending_count == 0) {
			break;
		}

		// Pop the promise with the highest index
		scoped_di_string key =
		    di_string_printf("pending_promise_%d", pending_count - 1);
		scoped_di_object *promise = NULL;
		DI_CHECK_OK(di_get2(eventm, key, promise));
		DI_CHECK_OK(di_delete_member_raw((void *)eventm, key));
		di_rawsetx((void *)eventm, di_string_borrow_literal("pending_count"),
		           DI_TYPE_NINT, (int[]){pending_count - 1});

		di_promise_dispatch((void *)promise);
	} while (true);

	auto roots = di_get_roots();
	di_call(roots, "remove_anonymous", (di_object *)eventm);
}

void di_event_module_dtor(di_object *obj) {
	auto em = (di_event_module *)obj;
	di_object *di_obj = di_object_borrow_deai(obj);
	if (di_obj) {
		auto di = (struct deai *)di_obj;
		ev_idle_stop(di->loop, &em->idlew);
	}
}

/// Core events
///
/// EXPORT: event: deai:module
///
/// Fundament event sources exposed by deai. This is the building blocks of other event
/// sources.
void di_init_event(struct deai *di) {
	auto em = di_new_module_with_size(di, sizeof(di_event_module));
	auto eventp = (di_event_module *)em;

	di_method(em, "fdevent", di_create_ioev, int);
	di_method(em, "timer", di_create_timer, double);
	di_method(em, "periodic", di_create_periodic, double, double);
	di_method(em, "new_promise", di_new_promise);
	di_method(em, "join_promises", di_join_promises, di_array);
	di_method(em, "any_promise", di_any_promise, di_array);

	auto dep = tmalloc(struct di_prepare, 1);
	dep->evm = em;
	ev_prepare_init(dep, di_prepare);
	ev_prepare_start(di->loop, (ev_prepare *)dep);

	ev_idle_init(&eventp->idlew, di_idle_cb);

	di_rawsetx((void *)em, di_string_borrow_literal("pending_count"), DI_TYPE_NINT,
	           (int[]){0});
	di_set_object_dtor((void *)em, di_event_module_dtor);
	di_register_module(di, di_string_borrow("event"), &em);
}
