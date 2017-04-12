#include "event_internal.h"
#include "di_internal.h"
#include "utils.h"
#include <ev.h>
#include <plugin.h>
struct di_ioev {
	struct di_object;
	ev_io evh;
	struct ev_loop *loop;
};

struct di_timer {
	struct di_object;
	ev_timer evt;
	struct ev_loop *loop;
};

struct di_evmodule {
	struct di_module;
	struct ev_loop *loop;
};

static void di_ioev_callback(EV_P_ ev_io *w, int revents) {
	auto ev = container_of(w, struct di_ioev, evh);
	if (revents & EV_READ)
		di_emit_signal((void *)ev, "read", NULL);
	if (revents & EV_WRITE)
		di_emit_signal((void *)ev, "write", NULL);
}

static void di_timer_callback(EV_P_ ev_timer *t, int revents) {
	auto d = container_of(t, struct di_timer, evt);
	di_emit_signal_v((void *)d, "elapsed", ev_now(EV_A));
}

static void di_start_ioev(struct di_object *obj) {
	struct di_ioev *ev = (void *)obj;
	ev_io_start(ev->loop, &ev->evh);
}

static void di_ioev_dtor(struct di_object *obj) {
	struct di_ioev *ev = (void *)obj;
	ev_io_stop(ev->loop, &ev->evh);
}

static void di_timer_dtor(struct di_object *obj) {
	struct di_timer *ev = (void *)obj;
	ev_timer_stop(ev->loop, &ev->evt);
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
	ret->loop = em->loop;

	auto startfn =
	    di_create_typed_method((di_fn_t)di_start_ioev, "start", DI_TYPE_VOID, 0);
	di_register_typed_method((void *)ret, (void *)startfn);

	auto dtor =
	    di_create_typed_method((di_fn_t)di_ioev_dtor, "__dtor", DI_TYPE_VOID, 0);
	di_register_typed_method((void *)ret, dtor);

	if (t & IOEV_READ)
		di_register_signal((void *)ret, "read", 0);
	if (t & IOEV_WRITE)
		di_register_signal((void *)ret, "write", 0);
	return (void *)ret;
}
static struct di_object *di_create_timer(struct di_object *obj, uint64_t timeout) {
	struct di_evmodule *em = (void *)obj;
	auto ret = di_new_object_with_type(struct di_timer);
	ret->loop = em->loop;

	auto dtor =
	    di_create_typed_method((di_fn_t)di_timer_dtor, "__dtor", DI_TYPE_VOID, 0);
	di_register_typed_method((void *)ret, dtor);

	ev_timer_init(&ret->evt, di_timer_callback, timeout, 0);
	di_register_signal((void *)ret, "elapsed", 1, DI_TYPE_FLOAT);
	ev_timer_start(em->loop, &ret->evt);
	return (void *)ret;
}
void di_init_event_module(struct deai *di) {
	auto em = di_new_module_with_type("event", struct di_evmodule);

	auto fn = di_create_typed_method((di_fn_t)di_create_ioev, "fdevent",
	                       DI_TYPE_OBJECT, 2, DI_TYPE_NINT, DI_TYPE_NINT);

	auto tfn = di_create_typed_method((di_fn_t)di_create_timer, "timer",
	                       DI_TYPE_OBJECT, 1, DI_TYPE_UINT);

	if (di_register_typed_method((void *)em, (void *)fn) != 0)
		goto out;
	fn = NULL;
	if (di_register_typed_method((void *)em, (void *)tfn) != 0)
		goto out;
	tfn = NULL;
	em->loop = di->loop;
	di_register_module(di, (void *)em);
out:
	free(fn);
	free(tfn);
	di_unref_object((void *)em);
}
