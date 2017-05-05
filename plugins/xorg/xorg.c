/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai.h>
#include <event.h>
#include <helper.h>
#include <log.h>

#include <xcb/xcb.h>
#include <assert.h>

#include "uthash.h"
#include "utils.h"

#include "xinput.h"
#include "xorg.h"

static void di_xorg_ioev(struct di_listener_data *l) {
	struct di_xorg_connection *dc = (void *)l->user_data;
	//di_get_log(dc->x->di);
	//di_log_va((void *)log, DI_LOG_DEBUG, "xcb ioev\n");

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
	di_unref_object((void *)x->dc);
	*(x->e) = NULL;
	x->free(x);
}

static void di_xorg_free_connection(struct di_xorg_connection *xc) {
	assert(!xc->xi);
	di_remove_listener(xc->xcb_fd, "read", xc->xcb_fdlistener);
	di_unref_object(xc->xcb_fd);
	xc->x = NULL;
	xcb_disconnect(xc->c);
}

static struct di_object *di_xorg_connect(struct di_xorg *x) {
	struct di_xorg_connection *dc =
	    di_new_object_with_type(struct di_xorg_connection);
	dc->c = xcb_connect(NULL, &dc->dflt_scrn);

	struct di_module *evm = di_find_module(x->di, "event");
	if (!evm)
		return NULL;

	di_call(evm, "fdevent", dc->xcb_fd, xcb_get_file_descriptor(dc->c), IOEV_READ);
	dc->xcb_fdlistener =
	    di_add_typed_listener(dc->xcb_fd, "read", dc, (di_fn_t)di_xorg_ioev);

	di_call0(dc->xcb_fd, "start");
	di_unref_object((void *)evm);

	di_register_typed_method(
	    (void *)dc, di_create_typed_method((di_fn_t)di_xorg_get_xinput,
	                                       "__get_xinput", DI_TYPE_OBJECT, 0));

	di_register_typed_method(
	    (void *)dc, di_create_typed_method((di_fn_t)di_xorg_free_connection,
	                                       "__dtor", DI_TYPE_VOID, 0));

	dc->x = x;
	return (void *)dc;
}
PUBLIC int di_plugin_init(struct deai *di) {
	auto x = di_new_module_with_type("xorg", struct di_xorg);
	x->di = di;

	di_register_typed_method(
	    (void *)x, di_create_typed_method((di_fn_t)di_xorg_connect, "connect",
	                                      DI_TYPE_OBJECT, 0));

	di_register_module(di, (void *)x);
	return 0;
}
