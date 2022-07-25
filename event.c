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
	double at;
	bool spent;
};

struct di_periodic {
	struct di_object_internal;
	ev_periodic pt;
	uint64_t root_handle;
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
/// EXPORT: deai.builtin.event:IoEv.start(): :void
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
/// EXPORT: deai.builtin.event:IoEv.stop(): :void
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
/// EXPORT: event.fdevent(fd: :integer, flag: :integer): deai.builtin.event:IoEv
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

static void di_timer_delete_signal(struct di_object *o) {
	if (di_remove_member_raw(o, di_string_borrow("__signal_elapsed")) != 0) {
		return;
	}

	auto t = (struct di_timer *)o;
	auto roots = di_get_roots();

	// Ignore error because roots might have been removed by di:exit
	di_string_with_cleanup timer_key = di_string_printf("___timer_%p", o);
	di_call(roots, "remove", timer_key);

	di_object_with_cleanup di_obj = di_object_get_deai_strong(o);
	auto di = (struct deai *)di_obj;
	ev_timer_stop(di->loop, &t->evt);
	di_object_downgrade_deai(o);
}

static void di_timer_add_signal(struct di_object *o, struct di_object *sig) {
	auto t = (struct di_timer *)o;
	if (t->spent) {
		return;
	}

	di_object_with_cleanup di_obj = di_object_get_deai_weak(o);
	if (di_obj == NULL) {
		return;
	}

	if (di_member_clone(o, "__signal_elapsed", sig) != 0) {
		return;
	}

	di_object_upgrade_deai(o);

	// Add ourselve to GC root
	auto roots = di_get_roots();
	di_string_with_cleanup timer_key = di_string_printf("___timer_%p", o);
	if (di_call(roots, "add", timer_key, o) != 0) {
		// Could happen if di:exit is called
		di_timer_delete_signal(o);
	}
	auto di = (struct deai *)di_obj;

	// Recalculate timeout
	double after = t->at - ev_now(di->loop);
	ev_timer_set(&t->evt, after, 0.0);
	ev_timer_start(di->loop, &t->evt);
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
static struct di_object *di_create_timer(struct di_object *obj, double timeout) {
	struct di_module *em = (void *)obj;
	auto ret = di_new_object_with_type(struct di_timer);
	di_set_type((void *)ret, "deai.builtin.event:Timer");
	di_object_with_cleanup di_obj = di_module_get_deai(em);
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
	auto weak_di = di_weakly_ref_object(di_obj);
	di_member(ret, DEAI_MEMBER_NAME_RAW, weak_di);
	return (struct di_object *)ret;
}

static void periodic_dtor(struct di_periodic *p) {
	if (p->root_handle == 0) {
		return;
	}

	di_object_with_cleanup di_obj = di_object_get_deai_strong((struct di_object *)p);
	auto di = (struct deai *)di_obj;
	ev_periodic_stop(di->loop, &p->pt);
}

/// Update timer interval and offset
///
/// EXPORT: deai.builtin.event:Periodic.set(interval: :float, offset: :float): :void
///
/// Timer will be reset after update.
static void periodic_set(struct di_periodic *p, double interval, double offset) {
	di_object_with_cleanup di_obj = di_object_get_deai_strong((struct di_object *)p);
	DI_CHECK(di_obj != NULL);
	ev_periodic_set(&p->pt, offset, interval, NULL);

	auto di = (struct deai *)di_obj;
	ev_periodic_again(di->loop, &p->pt);
}

static void di_periodic_signal_setter(struct di_object *o, struct di_object *sig) {
	if (di_member_clone(o, di_signal_member_of("triggered"), sig) != 0) {
		return;
	}
	struct di_periodic *p = (void *)o;
	auto roots = di_get_roots();
	DI_CHECK_OK(di_callr(roots, "__add_anonymous", p->root_handle, o));
	di_object_upgrade_deai(o);
}

static void di_periodic_signal_deleter(struct di_object *o) {
	di_remove_member_raw(o,
	                     di_string_borrow_literal(di_signal_member_of("triggered")));

	struct di_periodic *p = (void *)o;
	auto roots = di_get_roots();
	di_call(roots, "__remove_anonymous", p->root_handle);
	di_object_downgrade_deai(o);
	p->root_handle = 0;
}

/// Periodic timer event
///
/// EXPORT: event.periodic(interval: :float, offset: :float): deai.builtin.event:Periodic
///
/// A timer that first fire after :code:`offset` seconds, then every :code:`interval`
/// seconds.
static struct di_object *
di_create_periodic(struct di_module *evm, double interval, double offset) {
	auto ret = di_new_object_with_type(struct di_periodic);
	di_set_type((void *)ret, "deai.builtin.event:Periodic");
	di_object_with_cleanup di_obj = di_module_get_deai(evm);

	ret->dtor = (void *)periodic_dtor;
	di_method(ret, "set", periodic_set, double, double);
	ev_periodic_init(&ret->pt, di_periodic_callback, offset, interval, NULL);
	di_signal_setter_deleter(ret, "triggered", di_periodic_signal_setter,
	                         di_periodic_signal_deleter);

	auto di = (struct deai *)di_obj;
	ev_periodic_start(di->loop, &ret->pt);

	auto weak_di = di_weakly_ref_object(di_obj);
	di_member(ret, DEAI_MEMBER_NAME_RAW, weak_di);
	return (void *)ret;
}

struct di_prepare {
	ev_prepare;
	struct di_module *evm;
};

static void di_prepare(EV_P_ ev_prepare *w, int revents) {
	di_collect_garbage();

	bool has_cycle;
	if (di_mark_and_sweep(&has_cycle)) {
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

/// A pending value
///
/// TYPE: deai:Promise
///
/// This encapsulates a pending value. Once this value become available, a "resolved"
/// signal will be emitted with the value. Each promise should resolve only once ever.
struct di_promise {
	struct di_object;
};

struct di_object *di_promise_then(struct di_object *promise, struct di_object *handler);
void di_resolve_promise(struct di_promise *promise, struct di_variant var);
static void di_promise_then_impl(struct di_promise *promise, struct di_promise *then_promise,
                                 struct di_object *handler);

static int di_promise_dispatch(struct di_object *prepare_handler, di_type_t *rt,
                        union di_value *r, struct di_tuple args) {
	di_object_with_cleanup promise_;
	if (di_get(prepare_handler, "promise", promise_) != 0) {
		return 0;
	}

	struct di_promise *promise = (void *)promise_;
	struct di_variant resolved;
	uint64_t nhandlers;
	DI_CHECK_OK(di_get(promise, "___resolved", resolved));
	DI_CHECK_OK(di_get(promise, "___n_handlers", nhandlers));

	// Reset number of handlers
	di_setx((void *)promise, di_string_borrow("___n_handlers"), DI_TYPE_UINT,
	        (uint64_t[]){0});
	auto handlers = tmalloc(struct di_object *, nhandlers);
	auto then_promises = tmalloc(struct di_object *, nhandlers);

	// Copy out all the handlers and promises in case they got overwritten by the
	// handlers.
	for (uint64_t i = 0; i < nhandlers; i++) {
		{
			char *buf;
			struct di_string str;
			str.length = asprintf(&buf, "___then_handler_%lu", i);
			str.data = buf;
			if (di_get(promise, buf, handlers[i]) == 0) {
				di_remove_member((void *)promise, str);
			} else {
				handlers[i] = NULL;
			}
			free(buf);

			str.length = asprintf(&buf, "___then_promise_%lu", i);
			str.data = buf;
			DI_CHECK_OK(di_get(promise, buf, then_promises[i]));
			di_remove_member((void *)promise, str);
			free(buf);
		}
	}

	for (uint64_t i = 0; i < nhandlers; i++) {
		union di_value return_value;
		di_type_t return_type;
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
	// Stop listening for the signal
	di_remove_member((void *)promise, di_string_borrow("___auto_listen_handle"));
	return 0;
}

/// Create a new promise object
///
/// EXPORT: event.new_promise(): deai:Promise
struct di_object *di_new_promise(struct di_object *event_module) {
	struct di_promise *ret = di_new_object_with_type(struct di_promise);
	auto weak_event = di_weakly_ref_object(event_module);
	di_set_type((void *)ret, "deai:Promise");
	di_member(ret, "___weak_event_module", weak_event);
	di_setx((void *)ret, di_string_borrow("___n_handlers"), DI_TYPE_UINT, (uint64_t[]){0});
	di_method(ret, "then", di_promise_then, struct di_object *);
	// "then" is a keyword in lua
	di_method(ret, "then_", di_promise_then, struct di_object *);
	di_method(ret, "resolve", di_resolve_promise, struct di_variant);
	return (void *)ret;
}

static void di_promise_start_dispatch(struct di_promise *promise) {
	if (di_lookup((void *)promise, di_string_borrow("___auto_listen_handle"))) {
		// Already started dispatch
		return;
	}

	di_object_with_cleanup handler = di_new_object_with_type(struct di_object);
	di_weak_object_with_cleanup weak_event = NULL;
	DI_CHECK_OK(di_get(promise, "___weak_event_module", weak_event));
	di_object_with_cleanup event_module = di_upgrade_weak_ref(weak_event);
	if (event_module == NULL) {
		return;
	}

	di_member_clone(handler, "promise", (struct di_object *)promise);
	di_set_object_call(handler, di_promise_dispatch);

	// Use a 0 second timer because prepare isn't guaranteed to be called if
	// epoll blocks.
	di_object_with_cleanup timer = NULL;
	if (di_callr(event_module, "timer", timer, 0.0) != 0) {
		return;
	}

	auto listen_handle =
	    di_listen_to(timer, di_string_borrow("elapsed"), handler);
	DI_CHECK_OK(di_call(listen_handle, "auto_stop", true));
	di_member(promise, "___auto_listen_handle", listen_handle);
}

static void di_promise_then_impl(struct di_promise *promise, struct di_promise *then_promise,
                                 struct di_object *handler) {
	uint64_t nhandlers;
	DI_CHECK_OK(di_get(promise, "___n_handlers", nhandlers));
	char *buf;
	if (handler) {
		asprintf(&buf, "___then_handler_%lu", nhandlers);
		di_member_clone(promise, buf, handler);
		free(buf);
	}
	asprintf(&buf, "___then_promise_%lu", nhandlers);
	di_member_clone(promise, buf, (struct di_object *)then_promise);
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
struct di_object *di_promise_then(struct di_object *promise, struct di_object *handler) {
	di_weak_object_with_cleanup weak_event = NULL;
	if (di_get(promise, "___weak_event_module", weak_event) != 0) {
		return di_new_error("Event module member not found");
	}
	di_object_with_cleanup event_module = di_upgrade_weak_ref(weak_event);
	if (event_module == NULL) {
		return di_new_error("deai shutting down?");
	}

	struct di_object *ret = di_new_promise(event_module);
	di_promise_then_impl((void *)promise, (void *)ret, handler);
	return ret;
}

static void di_promise_collect_handler(int index, struct di_object *storage,
                                       struct di_object *then_promise, struct di_variant var) {
	di_string_with_cleanup key = di_string_printf("%d", index);
	DI_CHECK_OK(di_add_member_clone(storage, key, DI_TYPE_VARIANT, &var));

	int left;
	DI_CHECK_OK(di_get(storage, "left", left));
	left -= 1;
	DI_CHECK_OK(di_setx(storage, di_string_borrow_literal("left"), DI_TYPE_NINT, &left));

	if (left == 0) {
		int total;
		DI_CHECK_OK(di_get(storage, "total", total));
		di_tuple_with_cleanup results = {
		    .length = total, .elements = tmalloc(struct di_variant, total)};
		for (int i = 0; i < total; i++) {
			di_string_with_cleanup key = di_string_printf("%d", i);
			union di_value tmp;
			DI_CHECK_OK(di_getxt(storage, key, DI_TYPE_VARIANT, &tmp));
			results.elements[i] = tmp.variant;
		}
		di_call(then_promise, "resolve", results);
	}
}

/// Create a promise that resolves when all given promises resolve
///
/// EXPORT: event.collect_promises(promises: [deai:Promise]): deai:Promise
struct di_object *di_collect_promises(struct di_object *event_module, struct di_array promises) {
	if (promises.elem_type != DI_TYPE_OBJECT) {
		return di_new_error("promises must all be objects");
	}
	struct di_object **arr = promises.arr;
	for (int i = 0; i < promises.length; i++) {
		if (!di_check_type(arr[i], "deai:Promise")) {
			return di_new_error("not all objects are promise");
		}
	}
	auto ret = di_new_promise(event_module);
	di_object_with_cleanup storage = di_new_object_with_type(struct di_object);
	int cnt = 0;
	for (int i = 0; i < promises.length; i++) {
		di_object_with_cleanup handler = (void *)di_closure(
		    di_promise_collect_handler, (cnt, storage, ret), struct di_variant);
		if (di_call(arr[i], "then", handler) == 0) {
			cnt += 1;
		}
	}
	DI_CHECK_OK(di_setx(storage, di_string_borrow_literal("left"), DI_TYPE_NINT, &cnt));
	DI_CHECK_OK(di_setx(storage, di_string_borrow_literal("total"), DI_TYPE_NINT, &cnt));
	return ret;
}

static void di_any_promise_handler(struct di_object *then, struct di_variant var) {
	di_call(then, "resolve", var);
}

/// Create a promise that resolves when any of given promises resolve
///
/// EXPORT: event.any_promise(promises: [deai:Promise]): deai:Promise
struct di_object *di_any_promise(struct di_object *event_module, struct di_array promises) {
	if (promises.elem_type != DI_TYPE_OBJECT) {
		return di_new_error("promises must all be objects");
	}
	struct di_object **arr = promises.arr;
	for (int i = 0; i < promises.length; i++) {
		if (!di_check_type(arr[i], "deai:Promise")) {
			return di_new_error("not all objects are promise");
		}
	}
	auto ret = di_new_promise(event_module);
	for (int i = 0; i < promises.length; i++) {
		di_object_with_cleanup handler =
		    (void *)di_closure(di_any_promise_handler, (ret), struct di_variant);
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

/// Core events
///
/// EXPORT: event: deai:module
///
/// Fundament event sources exposed by deai. This is the building blocks of other event
/// sources.
void di_init_event(struct deai *di) {
	auto em = di_new_module(di);

	di_method(em, "fdevent", di_create_ioev, int);
	di_method(em, "timer", di_create_timer, double);
	di_method(em, "periodic", di_create_periodic, double, double);
	di_method(em, "new_promise", di_new_promise);
	di_method(em, "collect_promises", di_collect_promises, struct di_array);
	di_method(em, "any_promise", di_any_promise, struct di_array);

	auto dep = tmalloc(struct di_prepare, 1);
	dep->evm = em;
	ev_prepare_init(dep, di_prepare);
	ev_prepare_start(di->loop, (ev_prepare *)dep);

	di_register_module(di, di_string_borrow("event"), &em);
}
