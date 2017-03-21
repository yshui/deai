#include "event.h"
#include "di_internal.h"
#include "utils.h"
#include <ev.h>
#include <plugin.h>
struct di_ioev {
	struct di_object;
	ev_io evh;
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

static void di_start_ioev(struct di_object *obj) {
	struct di_ioev *ev = (void *)obj;
	ev_io_start(ev->loop, &ev->evh);
}

static void di_ioev_dtor(struct di_object *obj) {
	struct di_ioev *ev = (void *)obj;
	ev_io_stop(ev->loop, &ev->evh);
}
static struct di_object *di_create_ioev(struct di_object *obj, int32_t fd, int32_t t) {
	struct di_evmodule *em = (void *)obj;
	struct di_ioev *ret = tmalloc(struct di_ioev, 1);

	unsigned int flags = 0;
	if (t & IOEV_READ)
		flags |= EV_READ;
	if (t & IOEV_WRITE)
		flags |= EV_WRITE;

	ev_io_init(&ret->evh, di_ioev_callback, fd, flags);
	ret->loop = em->loop;

	auto startfn =
	    di_create_typed_method((di_fn_t)di_start_ioev, "start", DI_TYPE_VOID, 0);
	di_register_method((void *)ret, (void *)startfn);

	auto dtor =
	    di_create_typed_method((di_fn_t)di_ioev_dtor, "__dtor", DI_TYPE_VOID, 0);
	di_register_typed_method((void *)ret, dtor);

	di_register_signal((void *)ret, "read", 0);
	di_register_signal((void *)ret, "write", 0);
	return (void *)ret;
}
struct di_module *di_init_event_module(struct deai *di) {
	auto em = di_new_module_with_type("event", struct di_evmodule);

	auto fn = di_create_typed_method((di_fn_t)di_create_ioev, "fdevent",
	                       DI_TYPE_OBJECT, 2, DI_TYPE_INT32, DI_TYPE_INT32);

	if (di_register_typed_method((void *)em, (void *)fn) != 0) {
		free(fn);
		di_free_module((void *)em);
	}
	em->loop = di->loop;
	return (void *)em;
}
