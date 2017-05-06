/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai.h>
#include <event.h>
#include <helper.h>
#include <log.h>

#include <assert.h>
#include <stdio.h>
#include <xcb/xcb.h>

#include "uthash.h"
#include "utils.h"

#include "xinput.h"
#include "xorg.h"

struct di_atom_entry {
	char *name;
	xcb_atom_t atom;

	UT_hash_handle hh, hh2;
};

define_trivial_cleanup_t(xcb_generic_error_t);

static void di_xorg_ioev(struct di_listener_data *l) {
	struct di_xorg_connection *dc = (void *)l->user_data;
	// di_get_log(dc->x->di);
	// di_log_va((void *)log, DI_LOG_DEBUG, "xcb ioev\n");

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

const char *di_xorg_get_atom_name(struct di_xorg_connection *xc, xcb_atom_t atom) {
	struct di_atom_entry *ae = NULL;
	HASH_FIND(hh, xc->a_byatom, &atom, sizeof(atom), ae);
	if (ae)
		return ae->name;

	auto r = xcb_get_atom_name_reply(xc->c, xcb_get_atom_name(xc->c, atom), NULL);
	if (!r)
		return NULL;

	ae = tmalloc(struct di_atom_entry, 1);
	ae->name =
	    strndup(xcb_get_atom_name_name(r), xcb_get_atom_name_name_length(r));
	ae->atom = atom;
	free(r);

	HASH_ADD(hh, xc->a_byatom, atom, sizeof(xcb_atom_t), ae);
	HASH_ADD_KEYPTR(hh2, xc->a_byname, ae->name, strlen(ae->name), ae);

	return ae->name;
}

xcb_atom_t di_xorg_intern_atom(struct di_xorg_connection *xc, const char *name,
                               xcb_generic_error_t **e) {
	struct di_atom_entry *ae = NULL;
	*e = NULL;

	HASH_FIND(hh2, xc->a_byname, name, strlen(name), ae);
	if (ae)
		return ae->atom;

	auto r = xcb_intern_atom_reply(
	    xc->c, xcb_intern_atom(xc->c, 0, strlen(name), name), e);
	if (!r)
		return 0;

	ae = tmalloc(struct di_atom_entry, 1);
	ae->atom = r->atom;
	ae->name = strdup(name);
	free(r);

	HASH_ADD(hh, xc->a_byatom, atom, sizeof(xcb_atom_t), ae);
	HASH_ADD_KEYPTR(hh2, xc->a_byname, ae->name, strlen(ae->name), ae);

	return ae->atom;
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

	struct di_atom_entry *ae, *tae;
	HASH_ITER(hh, xc->a_byatom, ae, tae) {
		HASH_DEL(xc->a_byatom, ae);
		HASH_DELETE(hh2, xc->a_byname, ae);
		free(ae->name);
		free(ae);
	}
}

static char *di_xorg_get_resource(struct di_xorg_connection *xc) {
	auto scrn = screen_of_display(xc->c, xc->dflt_scrn);
	auto r = xcb_get_property_reply(
	    xc->c, xcb_get_property(xc->c, 0, scrn->root, XCB_ATOM_RESOURCE_MANAGER,
	                            XCB_ATOM_ANY, 0, 0),
	    NULL);
	if (!r)
		return strdup("");

	auto real_size = r->bytes_after;
	free(r);

	r = xcb_get_property_reply(
	    xc->c, xcb_get_property(xc->c, 0, scrn->root, XCB_ATOM_RESOURCE_MANAGER,
	                            XCB_ATOM_ANY, 0, real_size),
	    NULL);
	if (!r)
		return strdup("");

	char *ret =
	    strndup(xcb_get_property_value(r), xcb_get_property_value_length(r));
	free(r);
	return ret;
}

static void di_xorg_set_resource(struct di_xorg_connection *xc, const char *rdb) {
	auto scrn = screen_of_display(xc->c, xc->dflt_scrn);
	with_cleanup_t(xcb_generic_error_t) e = xcb_request_check(
	    xc->c, xcb_change_property(xc->c, XCB_PROP_MODE_REPLACE, scrn->root,
	                               XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, 8,
	                               strlen(rdb), rdb));
	(void)e;
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
	    (void *)dc, di_create_typed_method((di_fn_t)di_xorg_get_resource,
	                                       "__get_xrdb", DI_TYPE_STRING, 0));

	di_register_typed_method(
	    (void *)dc, di_create_typed_method((di_fn_t)di_xorg_set_resource,
	                                       "__set_xrdb", DI_TYPE_VOID, 1, DI_TYPE_STRING));

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
