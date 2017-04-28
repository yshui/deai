#include <deai.h>
#include <event.h>
#include <log.h>

#include <xcb/xcb.h>

#include "uthash.h"
#include "utils.h"

#include "xinput.h"
#include "xorg.h"

static void di_xorg_ioev(struct di_listener_data *l) {
	struct di_xorg_connection *dc = (void *)l->user_data;
	di_get_log(dc->x->di);
	di_log_va((void *)log, DI_LOG_DEBUG, "xcb ioev\n");

	xcb_generic_event_t *ev;

	while ((ev = xcb_poll_for_event(dc->c))) {
		// handle event

		if (ev->response_type == XCB_GE_GENERIC) {
			auto gev = (xcb_ge_generic_event_t *)ev;
			if (gev->extension == dc->xi->opcode)
				dc->xi->handle_event(dc->xi, gev);
		}
		free(ev);
	}
}

void di_xorg_free_sub(struct di_xorg_ext *x) {
	*(x->e) = NULL;
	x->free(x);
}

static struct di_object *di_xorg_connect(struct di_xorg *x) {
	struct di_xorg_connection *dc =
	    di_new_object_with_type(struct di_xorg_connection);
	dc->c = xcb_connect(NULL, &dc->dflt_scrn);

	struct di_module *evm = di_find_module(x->di, "event");
	if (!evm)
		return NULL;

	struct di_method *m = di_find_method((void *)evm, "fdevent");
	void *ret;
	di_type_t rtype;
	di_call_callable_v((void *)m, &rtype, &ret, DI_TYPE_NINT,
	                   xcb_get_file_descriptor(dc->c), DI_TYPE_NINT, IOEV_READ,
	                   DI_LAST_TYPE);

	struct di_object *xcbfd = *(struct di_object **)ret;
	free(ret);

	dc->l = di_add_typed_listener(xcbfd, "read", dc, (di_fn_t)di_xorg_ioev);
	di_unref_object(xcbfd);        // kept alive by the listener

	m = di_find_method(xcbfd, "start");
	di_call_callable_v((void *)m, &rtype, &ret, DI_LAST_TYPE);

	auto tm = di_create_typed_method((di_fn_t)di_xorg_get_xinput, "__get_xinput",
	                                 DI_TYPE_OBJECT, 0);
	di_register_typed_method((void *)dc, tm);

	dc->x = x;
	list_add(&dc->siblings, &x->connections);

	return (void *)dc;
}
PUBLIC int di_plugin_init(struct deai *di) {
	struct di_xorg *x = di_new_module_with_type("xorg", struct di_xorg);
	x->di = di;

	struct di_typed_method *conn = di_create_typed_method(
	    (di_fn_t)di_xorg_connect, "connect", DI_TYPE_OBJECT, 0);
	di_register_typed_method((void *)x, conn);

	INIT_LIST_HEAD(&x->connections);

	di_register_module(di, (void *)x);
	return 0;
}
