/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai/builtin/event.h>
#include <deai/builtin/log.h>
#include <deai/deai.h>
#include <deai/helper.h>

#include <assert.h>
#include <stdio.h>
#include <xcb/xcb.h>

#include "uthash.h"
#include "utils.h"

#include "xorg.h"

struct di_atom_entry {
	char *name;
	xcb_atom_t atom;

	UT_hash_handle hh, hh2;
};

define_trivial_cleanup_t(xcb_generic_error_t);

void di_xorg_free_sub(struct di_xorg_ext *x) {
	if (x->dc) {
		if (x->free)
			x->free(x);
		HASH_DEL(x->dc->xext, x);
		di_unref_object((void *)x->dc);
		x->dc = NULL;
	}
	di_clear_listener((void *)x);
}

static void xorg_disconnect(struct di_xorg_connection *xc) {
	if (!xc->xcb_fd)
		return;

	di_unref_object((void *)xc->xcb_fd);
	xc->xcb_fd = NULL;

	// free_sub might need the connection, don't disconnect now
	struct di_xorg_ext *ext, *text;
	HASH_ITER(hh, xc->xext, ext, text) {
		di_xorg_free_sub(ext);
	}
	xcb_disconnect(xc->c);
	xc->x = NULL;

	struct di_atom_entry *ae, *tae;
	HASH_ITER(hh, xc->a_byatom, ae, tae) {
		HASH_DEL(xc->a_byatom, ae);
		HASH_DELETE(hh2, xc->a_byname, ae);
		free(ae->name);
		free(ae);
	}

	di_unref_object((void *)xc->xcb_fdlistener);
	di_apoptosis((void *)xc);
}

static void di_xorg_ioev(struct di_xorg_connection *dc, struct di_object *fd) {
	// di_get_log(dc->x->di);
	// di_log_va((void *)log, DI_LOG_DEBUG, "xcb ioev\n");

	xcb_generic_event_t *ev;

	while ((ev = xcb_poll_for_event(dc->c))) {
		// handle event

		struct di_xorg_ext *ex, *tmp;
		HASH_ITER(hh, dc->xext, ex, tmp) {
			int status = ex->handle_event(ex, ev);
			if (status != 1)
				break;
		}
		free(ev);
	}

	if (xcb_connection_has_error(dc->c)) {
		di_emit(dc, "connection-error");
		xorg_disconnect(dc);
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
	di_getm(xc->x->di, log, 0);
	struct di_atom_entry *ae = NULL;
	*e = NULL;

	HASH_FIND(hh2, xc->a_byname, name, strlen(name), ae);
	if (ae)
		return ae->atom;

	auto r = xcb_intern_atom_reply(
	    xc->c, xcb_intern_atom(xc->c, 0, strlen(name), name), e);
	if (!r) {
		di_log_va(logm, DI_LOG_ERROR, "Cannot intern atom");
		return 0;
	}

	ae = tmalloc(struct di_atom_entry, 1);
	ae->atom = r->atom;
	ae->name = strdup(name);
	free(r);

	HASH_ADD(hh, xc->a_byatom, atom, sizeof(xcb_atom_t), ae);
	HASH_ADD_KEYPTR(hh2, xc->a_byname, ae->name, strlen(ae->name), ae);

	return ae->atom;
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

struct _xext {
	const char *name;
	struct di_xorg_ext *(*new)(struct di_xorg_connection *xc);
} xext_reg[] = {
    {"xinput", new_xinput},
    {"randr", new_randr},
    {"key", new_key},
    {NULL, NULL},
};

static struct di_object *
di_xorg_get_ext(struct di_xorg_connection *xc, const char *name) {
	struct di_xorg_ext *ret;
	HASH_FIND_STR(xc->xext, name, ret);
	if (ret) {
		di_ref_object((void *)ret);
		return (void *)ret;
	}
	for (int i = 0; xext_reg[i].name; i++)
		if (strcmp(xext_reg[i].name, name) == 0) {
			auto ext = xext_reg[i].new(xc);
			ext->dtor = (void *)di_xorg_free_sub;

			HASH_ADD_KEYPTR(hh, xc->xext, ext->extname,
			                strlen(ext->extname), ext);
			di_ref_object((void *)xc);
			return (void *)ext;
		}
	return NULL;
}

struct xscreen {
	struct di_object;
	uint64_t width, height;
};
static struct di_object *get_screen(struct di_xorg_connection *dc) {
	auto scrn = screen_of_display(dc->c, dc->dflt_scrn);

	auto ret = di_new_object_with_type(struct xscreen);
	ret->height = scrn->height_in_pixels;
	ret->width = scrn->width_in_pixels;

	di_add_address_member((void *)ret, "height", false, DI_TYPE_UINT,
	                      &ret->height);
	di_add_address_member((void *)ret, "width", false, DI_TYPE_UINT, &ret->width);

	return (void *)ret;
}

static void xorg_pre_disconnect(struct di_xorg_connection *xc) {
	di_stop_listener(xc->xcb_fdlistener);
}

static struct di_object *
di_xorg_connect_to(struct di_xorg *x, const char *displayname) {
	int scrn;
	auto c = xcb_connect(displayname, &scrn);
	if (xcb_connection_has_error(c)) {
		xcb_disconnect(c);
		return di_new_error("Cannot connect to the display");
	}

	di_getm(x->di, event, di_new_error("Can't get event module"));

	struct di_xorg_connection *dc =
	    di_new_object_with_type(struct di_xorg_connection);
	dc->c = c;
	dc->dflt_scrn = scrn;

	di_callr(eventm, "fdevent", dc->xcb_fd, xcb_get_file_descriptor(dc->c),
	         IOEV_READ);

	struct di_object *odc = (void *)dc;
	auto cl = di_closure(di_xorg_ioev, true, (odc), struct di_object *);
	dc->xcb_fdlistener = di_listen_to(dc->xcb_fd, "read", (void *)cl);
	di_unref_object((void *)cl);

	dc->dtor = (void *)xorg_pre_disconnect;

	ABRT_IF_ERR(di_set_detach(dc->xcb_fdlistener,
	                          (di_detach_fn_t)xorg_disconnect, (void *)dc));

	di_call(dc->xcb_fd, "start");

	di_method(dc, "__get", di_xorg_get_ext, char *);
	di_method(dc, "__get_xrdb", di_xorg_get_resource);
	di_method(dc, "__set_xrdb", di_xorg_set_resource, char *);
	di_method(dc, "__get_screen", get_screen);
	di_method(dc, "disconnect", xorg_pre_disconnect);

	dc->x = x;
	return (void *)dc;
}

static struct di_object *di_xorg_connect(struct di_xorg *x) {
	return di_xorg_connect_to(x, NULL);
}
PUBLIC int di_plugin_init(struct deai *di) {
	auto x = di_new_module_with_type(struct di_xorg);
	x->di = di;

	di_method(x, "connect", di_xorg_connect);
	di_method(x, "connect_to", di_xorg_connect_to, char *);

	di_register_module(di, "xorg", (void *)x);
	di_unref_object((void *)x);
	return 0;
}
