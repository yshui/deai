/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai/builtins/event.h>
#include <deai/builtins/log.h>
#include <deai/deai.h>
#include <deai/error.h>
#include <deai/helper.h>
#include <deai/type.h>

#include <ev.h>
#include <unistd.h>

#include "di_internal.h"
#include "event.h"

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

static const char promise_type[] = "deai:Promise";

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
	if (di_delete_member_raw(obj, di_string_borrow_literal("__signal_read")) != 0) {
		return;
	}

	auto ioev = (struct di_ioev *)obj;
	unsigned int flags = ioev->evh.events & (~EV_READ);
	di_modify_ioev(ioev, flags);
}

static void di_disable_write(di_object *obj) {
	if (di_delete_member_raw(obj, di_string_borrow_literal("__signal_write")) != 0) {
		return;
	}

	auto ioev = (struct di_ioev *)obj;
	unsigned int flags = ioev->evh.events & (~EV_WRITE);
	di_modify_ioev(ioev, flags);
}

static void di_ioev_dtor(di_object *obj) {
	// Normally the ev_io won't be running, but if someone removed us from the roots,
	// e.g. by calling di:exit(), then ev_io could be running. Stop the event source
	di_modify_ioev((struct di_ioev *)obj, 0);
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
		di_throw(di_new_error("deai is shutting down..."));
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
	if (di_delete_member_raw(o, di_string_borrow_literal("__signal_elapsed")) != 0) {
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
		di_throw(di_new_error("deai is shutting down..."));
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

/// Call an array of handlers with a value. Consumes the handlers array.
static void di_handlers_dispatch(di_array handlers, di_variant value) {
	DI_CHECK(handlers.elem_type == DI_TYPE_OBJECT);

	di_tuple arg = {
	    .elements = &value,
	    .length = 1,
	};
	auto arr = (di_object **)handlers.arr;
	for (int i = 0; i < handlers.length; i++) {
		scoped_di_object *error = NULL;
		di_type rtype;
		di_value ret;
		int rc = di_call_object_catch(arr[i], &rtype, &ret, arg, &error);
		if (rc != 0) {
			log_error("Error calling handler: %d", rc);
		} else if (error != 0) {
			scoped_di_string error_message = di_object_to_string(error, NULL);
			log_error("Error in promise handler: %.*s", (int)error_message.length,
			          error_message.data);
		} else {
			di_free_value(rtype, &ret);
		}
		di_unref_object(arr[i]);
	}
	free(handlers.arr);
}
static int di_promise_dispatch(di_promise *promise) {
	scoped_di_variant resolved = DI_VARIANT_INIT;
	di_object *rejected = NULL;
	if (di_get(promise, "___resolved", resolved) == 0) {
		di_variant handlers_v;
		if (di_remove_member_raw((di_object *)promise,
		                         di_string_borrow_literal("___resolve_handlers"),
		                         &handlers_v) == 0) {
			di_array handlers = handlers_v.value->array;
			free(handlers_v.value);
			di_handlers_dispatch(handlers, resolved);
		}
	} else if (di_get(promise, "___rejected", rejected) == 0) {
		di_variant handlers_v;
		if (di_remove_member_raw((di_object *)promise,
		                         di_string_borrow_literal("___reject_handlers"),
		                         &handlers_v) == 0) {
			di_array handlers = handlers_v.value->array;
			di_variant var = {
			    .type = DI_TYPE_OBJECT,
			    .value = (di_value *)&rejected,
			};
			free(handlers_v.value);
			di_handlers_dispatch(handlers, var);
			di_unref_object(rejected);
			rejected = NULL;
		}
	}
	di_delete_member_raw((void *)promise, di_string_borrow_literal("is_pending"));
	if (rejected) {
		scoped_di_string error_message = di_object_to_string(rejected, NULL);
		log_error("Unhandled promise rejection: %.*s", (int)error_message.length,
		          error_message.data);
		di_unref_object(rejected);
	}
	return 0;
}

/// Create a new promise object
///
/// EXPORT: event.new_promise(): deai:Promise
di_object *di_new_promise(di_object *event_module) {
	struct di_promise *ret = di_new_object_with_type(struct di_promise);
	auto weak_event = di_weakly_ref_object(event_module);
	di_set_type((void *)ret, promise_type);
	di_member(ret, "___weak_event_module", weak_event);
	di_method(ret, "then", di_promise_then, di_object *);
	// "then" is a keyword in lua
	di_method(ret, "then_", di_promise_then, di_object *);
	di_method(ret, "resolve", di_promise_resolve, struct di_variant);
	di_method(ret, "reject", di_promise_reject, di_object *);
	return (void *)ret;
}

static void di_promise_start_dispatch(struct di_promise *promise) {
	if (di_lookup((void *)promise, di_string_borrow_literal("is_pending"))) {
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

static int di_promise_then_closure(di_object *closure, di_type * /*type*/,
                                   di_value * /*ret*/, di_tuple args);

/// Add a listener to a promise, if the promise is already resolved, call the listener
/// during the next main loop iteration.
static void di_promise_then_inner(di_object *promise, di_object *closure) {
	struct di_member *handlers_member =
	    di_lookup(promise, di_string_borrow_literal("___resolve_handlers"));

	if (handlers_member == NULL) {
		handlers_member =
		    di_add_member_move2(promise, di_string_borrow_literal("___resolve_handlers"),
		                        (di_type[]){DI_TYPE_ARRAY},
		                        (di_array[]){{
		                            .arr = NULL,
		                            .length = 0,
		                            .elem_type = DI_TYPE_OBJECT,
		                        }});
	}
	di_array *handlers = &handlers_member->data->array;
	di_object **arr = handlers->arr;
	handlers->arr = arr = trealloc(arr, handlers->length + 1);
	arr[handlers->length++] = di_ref_object(closure);

	if (di_has_member(promise, "___resolved") || di_has_member(promise, "___rejected")) {
		di_promise_start_dispatch((struct di_promise *)promise);
	}
}

static int di_promise_then_closure(di_object *closure, di_type * /*type*/,
                                   di_value * /*ret*/, di_tuple args) {
	DI_CHECK(args.length == 1);
	di_object *promise = NULL;
	DI_CHECK_OK(di_rawget_borrowed(closure, "promise", promise));

	di_object *handler = NULL;
	di_variant handlerv = DI_VARIANT_INIT;
	// If there is no handler, this is a "verbatim" then closure, the resolved value is
	// directly passed to the subsequent promise. This behavior is also used below if the
	// handler returns a promise.
	if (di_remove_member_raw(closure, di_string_borrow_literal("handler"), &handlerv) != 0) {
		di_promise_resolve(promise, (struct di_variant){.type = args.elements[0].type,
		                                                .value = args.elements[0].value});
		di_delete_member_raw(closure, di_string_borrow_literal("promise"));
		return 0;
	}

	DI_CHECK(handlerv.type == DI_TYPE_OBJECT);
	handler = handlerv.value->object;
	free(handlerv.value);

	di_type rtype;
	di_value ret;
	di_object *error = NULL;
	int rc = di_call_object_catch(handler, &rtype, &ret, args, &error);
	di_unref_object(handler);
	if (rc != 0) {
		log_error("Error calling handler: %d", rc);
		return 0;
	}

	bool pending = false;
	if (error != NULL) {
		di_promise_reject(promise, error);
	} else if (rtype != DI_TYPE_OBJECT || !di_check_type(ret.object, promise_type)) {
		// The handler returned a plain value, resolve the subsequent promise with it
		di_promise_resolve(promise, (struct di_variant){.type = rtype, .value = &ret});
	} else {
		// The handler returned a promise, the subsequent promise should resolve when
		// the returned promise resolves. We have already removed the handler from the
		// closure, turning it into a "verbatim" closure, so we just add it to the listeners.
		di_promise_then_inner(ret.object, closure);
		pending = true;
	}
	di_free_value(rtype, &ret);
	if (!pending) {
		di_delete_member_raw(closure, di_string_borrow_literal("promise"));
	}
	return 0;
}

/// Chain computation to a promise
///
/// EXPORT: deai:Promise.then(handler: :object): deai:Promise
///
/// Register a handler to be called after `promise` resolves, the handler will be called
/// with the resolved value as argument.
///
/// Returns a promise that will be resolved after handler returns. If handler returns
/// another promise, then the promise returned by this function will resolve after the
/// promise returned by the handler resolves, otherwise the returned promise will resolve
/// to the value returned by the handler.
///
/// Note the handler will always be called after being registered, whether the
/// promise returned by this function is freed or not.
///
/// (this function is called "then\_" in lua, since "then" is a keyword)
di_object *di_promise_then(di_object *promise, di_object *handler) {
	scoped_di_weak_object *weak_event = NULL;
	if (di_get(promise, "___weak_event_module", weak_event) != 0) {
		di_throw(di_new_error("Event module member not found"));
	}
	scoped_di_object *event_module = di_upgrade_weak_ref(weak_event);
	if (event_module == NULL) {
		di_throw(di_new_error("deai shutting down?"));
	}

	di_object *ret = di_new_promise(event_module);

	di_object *closure = di_new_object_with_type_name(
	    sizeof(di_object), alignof(di_object), "deai.event:PromiseThenClosure");
	di_set_object_call(closure, di_promise_then_closure);
	di_member_clone(closure, "promise", ret);
	if (handler != NULL) {
		di_member_clone(closure, "handler", handler);
	}
	di_promise_then_inner(promise, closure);
	di_unref_object(closure);
	return ret;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void di_promise_join_handler(int index, di_object *storage,
                                    di_object *then_promise, struct di_variant var) {
	scoped_di_string key = di_string_printf("%d", index);
	DI_CHECK_OK(di_add_member_clone(storage, key, DI_TYPE_VARIANT, &var));

	int left;
	DI_CHECK_OK(di_get(storage, "left", left));
	left -= 1;
	DI_CHECK_OK(di_setx(storage, di_string_borrow_literal("left"), DI_TYPE_NINT, &left, NULL));

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
		di_throw(di_new_error("promises must all be objects"));
	}
	di_object **arr = promises.arr;
	for (int i = 0; i < promises.length; i++) {
		if (!di_check_type(arr[i], promise_type)) {
			di_throw(di_new_error("not all objects are promise"));
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
	DI_CHECK_OK(di_setx(storage, di_string_borrow_literal("left"), DI_TYPE_NINT, &cnt, NULL));
	DI_CHECK_OK(di_setx(storage, di_string_borrow_literal("total"), DI_TYPE_NINT, &cnt, NULL));
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
		di_throw(di_new_error("promises must all be objects"));
	}
	di_object **arr = promises.arr;
	for (int i = 0; i < promises.length; i++) {
		if (!di_check_type(arr[i], promise_type)) {
			di_throw(di_new_error("not all objects are promise"));
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

void di_promise_resolve(di_object *promise, struct di_variant var) {
	if (di_has_member(promise, "___resolved") || di_has_member(promise, "___rejected")) {
		// Already resolved
		return;
	}
	di_member_clone(promise, "___resolved", var);
	di_promise_start_dispatch((struct di_promise *)promise);
}

void di_promise_reject(di_object *promise, di_object *error) {
	if (di_has_member(promise, "___resolved") || di_has_member(promise, "___rejected")) {
		// Already resolved
		return;
	}
	di_member_clone(promise, "___rejected", error);
	di_promise_start_dispatch((struct di_promise *)promise);
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
		scoped_di_string key = di_string_printf("pending_promise_%d", pending_count - 1);
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

/// Create a new promise that is already resolved
///
/// EXPORT: event.ready_promise(value: :any): deai:Promise
///
/// Arguments:
///
/// - value what the returned promise will resolve to
di_object *di_ready_promise(di_object *event_module, di_variant value) {
	auto ret = di_new_promise(event_module);
	di_promise_resolve(ret, value);
	return ret;
}

/// Core events
///
/// EXPORT: event: deai:module
///
/// Fundament event sources exposed by deai. This is the building blocks of other event
/// sources.
void di_init_event(di_object *di) {
	auto em = di_new_module_with_size(di, sizeof(di_event_module));
	auto eventp = (di_event_module *)em;

	di_method(em, "fdevent", di_create_ioev, int);
	di_method(em, "timer", di_create_timer, double);
	di_method(em, "periodic", di_create_periodic, double, double);
	di_method(em, "new_promise", di_new_promise);
	di_method(em, "ready_promise", di_ready_promise, di_variant);
	di_method(em, "join_promises", di_join_promises, di_array);
	di_method(em, "any_promise", di_any_promise, di_array);

	auto dep = tmalloc(struct di_prepare, 1);
	dep->evm = em;
	ev_prepare_init(dep, di_prepare);
	ev_prepare_start(((struct deai *)di)->loop, (ev_prepare *)dep);

	ev_idle_init(&eventp->idlew, di_idle_cb);

	di_rawsetx((void *)em, di_string_borrow_literal("pending_count"), DI_TYPE_NINT, (int[]){0});
	di_set_object_dtor((void *)em, di_event_module_dtor);
	di_register_module(di, di_string_borrow_literal("event"), &em);
}
