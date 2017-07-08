/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai/builtin/log.h>
#include <deai/helper.h>

#include "randr.h"

#include <assert.h>
#include <stdio.h>
#include <xcb/randr.h>

struct di_xorg_randr {
	struct di_xorg_ext;

	int evbase;
	// uint16_t notify_mask;
	// unsigned listener_count[7];

	xcb_timestamp_t cts;        // config-ts
};

struct di_xorg_outputs {
	struct di_object;
	struct di_xorg_connection *dc;
};

struct di_xorg_output {
	struct di_object;
	struct di_xorg_randr *rr;

	xcb_randr_output_t oid;
};

// What xorg calls a crtc, we call a view.
//
// Who still has a CRT this day and age?
struct di_xorg_view {
	struct di_object;
	struct di_xorg_randr *rr;

	xcb_randr_crtc_t cid;
	xcb_timestamp_t ts;
};

struct di_xorg_mode {
	struct di_object;
	struct di_xorg_randr *rr;

	unsigned int id;
	unsigned int width;
	unsigned int height;
};

struct di_xorg_view_config {
	struct di_object;
	int64_t x, y;
	uint64_t width, height;
	uint64_t rotation, reflection;
};

define_trivial_cleanup_t(xcb_randr_get_screen_resources_reply_t);
define_trivial_cleanup_t(xcb_randr_get_screen_resources_current_reply_t);
define_trivial_cleanup_t(xcb_randr_get_output_info_reply_t);
define_trivial_cleanup_t(xcb_randr_get_crtc_info_reply_t);
define_trivial_cleanup_t(xcb_randr_query_output_property_reply_t);
define_trivial_cleanup_t(xcb_randr_get_output_property_reply_t);
define_trivial_cleanup_t(xcb_randr_set_crtc_config_reply_t);

static struct di_object *
make_object_for_view(struct di_xorg_randr *rr, xcb_randr_crtc_t cid);

static char *get_output_name(struct di_xorg_output *o) {
	with_cleanup_t(xcb_randr_get_output_info_reply_t) r =
	    xcb_randr_get_output_info_reply(
	        o->rr->dc->c,
	        xcb_randr_get_output_info(o->rr->dc->c, o->oid, o->rr->cts), NULL);

	char *ret = strndup((char *)xcb_randr_get_output_info_name(r),
	                    xcb_randr_get_output_info_name_length(r));
	return ret;
}

static struct di_object *get_output_view(struct di_xorg_output *o) {
	with_cleanup_t(xcb_randr_get_output_info_reply_t) r =
	    xcb_randr_get_output_info_reply(
	        o->rr->dc->c,
	        xcb_randr_get_output_info(o->rr->dc->c, o->oid, o->rr->cts), NULL);
	if (!r || r->status != 0 || r->crtc == 0)
		return NULL;
	return make_object_for_view(o->rr, r->crtc);
}

static struct di_object *get_view_config(struct di_xorg_view *v) {
	with_cleanup_t(xcb_randr_get_crtc_info_reply_t) cr =
	    xcb_randr_get_crtc_info_reply(
	        v->rr->dc->c,
	        xcb_randr_get_crtc_info(v->rr->dc->c, v->cid, v->rr->cts), NULL);
	if (!cr || cr->status != 0)
		return NULL;

	auto ret = di_new_object_with_type(struct di_xorg_view_config);
	ret->x = cr->x;
	ret->y = cr->y;
	ret->width = cr->width;
	ret->height = cr->height;

	if (cr->rotation & XCB_RANDR_ROTATION_ROTATE_0)
		ret->rotation = 0;
	else if (cr->rotation & XCB_RANDR_ROTATION_ROTATE_90)
		ret->rotation = 1;
	else if (cr->rotation & XCB_RANDR_ROTATION_ROTATE_180)
		ret->rotation = 2;
	else if (cr->rotation & XCB_RANDR_ROTATION_ROTATE_270)
		ret->rotation = 3;

	if (cr->rotation & XCB_RANDR_ROTATION_REFLECT_X) {
		if (cr->rotation & XCB_RANDR_ROTATION_REFLECT_Y)
			ret->reflection = 3;
		else
			ret->reflection = 1;
	} else if (cr->rotation & XCB_RANDR_ROTATION_REFLECT_Y)
		ret->reflection = 2;
	else
		ret->reflection = 0;

	di_field(ret, x);
	di_field(ret, y);
	di_field(ret, width);
	di_field(ret, height);
	di_field(ret, rotation);
	di_field(ret, reflection);
	return (void *)ret;
}

static void set_view_config(struct di_xorg_view *v, struct di_object *cfg) {
	int x, y;
	xcb_randr_mode_t mode;
	int rotation, reflection;
	uint16_t rr_rot;
	with_cleanup_t(xcb_randr_get_crtc_info_reply_t) cr = NULL;

	di_gets(cfg, "x", x);
	di_gets(cfg, "y", y);
	di_gets(cfg, "mode", mode);
	di_gets(cfg, "rotation", rotation);
	di_gets(cfg, "reflection", reflection);

	if (x < INT16_MIN || x > INT16_MAX)
		return;
	if (y < INT16_MIN || y > INT16_MAX)
		return;

	switch (rotation) {
	case 0: rr_rot = XCB_RANDR_ROTATION_ROTATE_0; break;
	case 1: rr_rot = XCB_RANDR_ROTATION_ROTATE_90; break;
	case 2: rr_rot = XCB_RANDR_ROTATION_ROTATE_180; break;
	case 3: rr_rot = XCB_RANDR_ROTATION_ROTATE_270; break;
	default: return;
	}
	if (reflection & 1)
		rr_rot |= XCB_RANDR_ROTATION_REFLECT_X;
	if (reflection & 2)
		rr_rot |= XCB_RANDR_ROTATION_REFLECT_Y;
retry:
	// We might need to retry under race conditions, because Xorg sucks
	cr = xcb_randr_get_crtc_info_reply(
	    v->rr->dc->c, xcb_randr_get_crtc_info(v->rr->dc->c, v->cid, v->rr->cts),
	    NULL);
	if (!cr || cr->status != 0)
		return;

	with_cleanup_t(xcb_randr_set_crtc_config_reply_t) scr =
	    xcb_randr_set_crtc_config_reply(
	        v->rr->dc->c,
	        xcb_randr_set_crtc_config(v->rr->dc->c, v->cid, cr->timestamp,
	                                  v->rr->cts, x, y, mode, rr_rot,
	                                  xcb_randr_get_crtc_info_outputs_length(cr),
	                                  xcb_randr_get_crtc_info_outputs(cr)),
	        NULL);
	if (!scr)
		return;
	if (scr->status == XCB_RANDR_SET_CONFIG_INVALID_TIME)
		goto retry;
}

static int get_output_backlight(struct di_xorg_output *o) {
	xcb_generic_error_t *e;
	auto bklatom = di_xorg_intern_atom(o->rr->dc, "Backlight", &e);
	if (e) {
		free(e);
		return -1;
	}
	with_cleanup_t(xcb_randr_get_output_property_reply_t) r =
	    xcb_randr_get_output_property_reply(
	        o->rr->dc->c,
	        xcb_randr_get_output_property(o->rr->dc->c, o->oid, bklatom,
	                                      XCB_ATOM_ANY, 0, 4, 0, 0),
	        NULL);

	if (!r || r->type != XCB_ATOM_INTEGER || r->num_items != 1 || r->format != 32)
		return -1;

	return *(int32_t *)xcb_randr_get_output_property_data(r);
}

static void set_output_backlight(struct di_xorg_output *o, int bkl) {
	xcb_generic_error_t *e;
	auto bklatom = di_xorg_intern_atom(o->rr->dc, "Backlight", &e);
	if (e) {
		free(e);
		return;
	}

	int32_t v = bkl;
	e = xcb_request_check(o->rr->dc->c,
	                      xcb_randr_change_output_property(
	                          o->rr->dc->c, o->oid, bklatom, XCB_ATOM_INTEGER,
	                          32, XCB_PROP_MODE_REPLACE, 1, (void *)&v));
	if (e) {
		di_getm(o->rr->dc->x->di, log, (void)0);
		di_log_va(logm, DI_LOG_ERROR, "Failed to set backlight");
	}
}

static int get_output_max_backlight(struct di_xorg_output *o) {
	xcb_generic_error_t *e;
	auto bklatom = di_xorg_intern_atom(o->rr->dc, "Backlight", &e);
	if (e) {
		free(e);
		return -1;
	}

	with_cleanup_t(xcb_randr_query_output_property_reply_t) r =
	    xcb_randr_query_output_property_reply(
	        o->rr->dc->c,
	        xcb_randr_query_output_property(o->rr->dc->c, o->oid, bklatom), NULL);
	if (!r)
		return -1;

	auto vv = xcb_randr_query_output_property_valid_values(r);
	auto vvl = xcb_randr_query_output_property_valid_values_length(r);
	if (vvl != 2)
		return -1;
	return vv[1];
}

static void output_dtor(struct di_xorg_output *o) {
	di_unref_object((void *)o->rr);
}

static struct di_object *
make_object_for_output(struct di_xorg_randr *rr, xcb_randr_output_t oid) {
	auto obj = di_new_object_with_type(struct di_xorg_output);
	obj->rr = rr;
	obj->oid = oid;
	di_getter(obj, view, get_output_view);
	di_getter(obj, name, get_output_name);
	di_getter_setter(obj, backlight, get_output_backlight, set_output_backlight);
	di_getter(obj, max_backlight, get_output_max_backlight);

	obj->dtor = (void *)output_dtor;

	di_ref_object((void *)rr);
	return (void *)obj;
}

static struct di_array get_view_outputs(struct di_xorg_view *v) {
	auto r = xcb_randr_get_crtc_info_reply(
	    v->rr->dc->c, xcb_randr_get_crtc_info(v->rr->dc->c, v->cid, v->rr->cts),
	    NULL);

	auto outputs = xcb_randr_get_crtc_info_outputs(r);
	struct di_array ret;
	ret.elem_type = DI_TYPE_OBJECT;
	ret.length = xcb_randr_get_crtc_info_outputs_length(r);
	void **arr = ret.arr = tmalloc(void *, ret.length);
	for (int i = 0; i < ret.length; i++)
		arr[i] = make_object_for_output(v->rr, outputs[i]);

	return ret;
}

static void view_dtor(struct di_xorg_view *v) {
	di_unref_object((void *)v->rr);
}

static struct di_object *
make_object_for_view(struct di_xorg_randr *rr, xcb_randr_crtc_t cid) {
	auto obj = di_new_object_with_type(struct di_xorg_view);
	obj->rr = rr;
	obj->cid = cid;

	di_getter(obj, outputs, get_view_outputs);
	di_getter_setter(obj, config, get_view_config, set_view_config);
	obj->dtor = (void *)view_dtor;

	di_ref_object((void *)rr);

	return (void *)obj;
}

static int handle_randr_event(struct di_xorg_ext *ext, xcb_generic_event_t *ev) {
	struct di_xorg_randr *rr = (void *)ext;
	if (ev->response_type == rr->evbase) {
		xcb_randr_screen_change_notify_event_t *sev = (void *)ev;
		rr->cts = sev->config_timestamp;
	} else if (ev->response_type == rr->evbase + 1) {
		xcb_randr_notify_event_t *rev = (void *)ev;
		if (rev->subCode == XCB_RANDR_NOTIFY_OUTPUT_CHANGE) {
			di_emit_from_object(
			    (void *)ext, "output-change",
			    make_object_for_output(rr, rev->u.oc.output));
			rr->cts = rev->u.oc.config_timestamp;
		} else if (rev->subCode == XCB_RANDR_NOTIFY_CRTC_CHANGE)
			di_emit_from_object((void *)ext, "view-change",
			                    make_object_for_view(rr, rev->u.cc.crtc));
	} else
		return 1;
	return 0;
}

static inline void rr_select_input(struct di_xorg_randr *rr, uint16_t mask) {
	auto scrn = screen_of_display(rr->dc->c, rr->dc->dflt_scrn);
	auto e = xcb_request_check(
	    rr->dc->c, xcb_randr_select_input(rr->dc->c, scrn->root, mask));

	di_getm(rr->dc->x->di, log, (void)0);
	if (e)
		di_log_va(logm, DI_LOG_ERROR, "randr select input failed\n");
}

#if 0
static void enable_randr_event(struct di_xorg_randr *rr, uint16_t mask) {
	// only allow single bit in mask
	assert(__builtin_popcount(mask) == 1);
	int id = __builtin_ffs(mask) - 1;

	rr->listener_count[id]++;
	if (rr->listener_count[id] > 1)
		return;

	assert(!(rr->notify_mask & mask));
	rr->notify_mask |= mask;

	randr_select_input(rr);
}

static void disable_randr_event(struct di_xorg_randr *rr, uint16_t mask) {
	assert(__builtin_popcount(mask) == 1);
	int id = __builtin_ffs(mask) - 1;

	rr->listener_count[id]--;
	if (rr->listener_count[id] > 0)
		return;

	assert(rr->notify_mask & mask);
	rr->notify_mask -= mask;

	randr_select_input(rr);
}

#define event_funcs(mask_name, name)                                                \
	static void enable_##name##_event(struct di_xorg_randr *rr) {               \
		enable_randr_event(rr, XCB_RANDR_NOTIFY_MASK_##mask_name);          \
	}                                                                           \
                                                                                    \
	static void disable_##name##_event(struct di_xorg_randr *rr) {              \
		disable_randr_event(rr, XCB_RANDR_NOTIFY_MASK_##mask_name);         \
	}
#endif

// event_funcs(OUTPUT_CHANGE, output_change);

static struct di_array rr_outputs(struct di_xorg_randr *rr) {
	struct di_array ret = DI_ARRAY_NIL;
	auto scrn = screen_of_display(rr->dc->c, rr->dc->dflt_scrn);
	with_cleanup_t(xcb_randr_get_screen_resources_current_reply_t) sr =
	    xcb_randr_get_screen_resources_current_reply(
	        rr->dc->c,
	        xcb_randr_get_screen_resources_current(rr->dc->c, scrn->root), NULL);
	if (!sr)
		return ret;

	fprintf(stderr, "%d\n", sr->config_timestamp);
	fprintf(stderr, "%d\n", sr->timestamp);
	ret.length = xcb_randr_get_screen_resources_current_outputs_length(sr);
	ret.elem_type = DI_TYPE_OBJECT;
	auto os = xcb_randr_get_screen_resources_current_outputs(sr);
	void **arr = ret.arr = tmalloc(void *, ret.length);
	for (int i = 0; i < ret.length; i++)
		arr[i] = make_object_for_output(rr, os[i]);

	return ret;
}

static struct di_object *
make_object_for_modes(struct di_xorg_randr *rr, xcb_randr_mode_info_t *m) {
	auto o = di_new_object_with_type(struct di_xorg_mode);
	o->id = m->id;
	o->width = m->width;
	o->height = m->height;

	di_field(o, width);
	di_field(o, height);
	di_field(o, id);

	return (void *)o;
}

static struct di_array rr_modes(struct di_xorg_randr *rr) {
	struct di_array ret = DI_ARRAY_NIL;
	auto scrn = screen_of_display(rr->dc->c, rr->dc->dflt_scrn);
	with_cleanup_t(xcb_randr_get_screen_resources_current_reply_t) sr =
	    xcb_randr_get_screen_resources_current_reply(
	        rr->dc->c,
	        xcb_randr_get_screen_resources_current(rr->dc->c, scrn->root), NULL);
	if (!sr)
		return ret;

	ret.length = xcb_randr_get_screen_resources_current_modes_length(sr);
	ret.elem_type = DI_TYPE_OBJECT;

	auto mi = xcb_randr_get_screen_resources_current_modes_iterator(sr);
	struct di_object **arr = ret.arr = tmalloc(struct di_object *, ret.length);
	for (int i = 0; mi.rem; xcb_randr_mode_info_next(&mi), i++)
		arr[i] = make_object_for_modes(rr, mi.data);

	return ret;
}

struct di_xorg_ext *di_xorg_new_randr(struct di_xorg_connection *dc) {
	char *extname = "RANDR";
	if (!xorg_has_extension(dc->c, extname))
		return NULL;

	auto cookie = xcb_query_extension(dc->c, strlen(extname), extname);
	auto r = xcb_query_extension_reply(dc->c, cookie, NULL);
	if (!r)
		return NULL;

	auto scrn = screen_of_display(dc->c, dc->dflt_scrn);
	with_cleanup_t(xcb_randr_get_screen_resources_current_reply_t) sr =
	    xcb_randr_get_screen_resources_current_reply(
	        dc->c, xcb_randr_get_screen_resources_current(dc->c, scrn->root),
	        NULL);
	if (!sr)
		return NULL;

	auto rr = di_new_object_with_type(struct di_xorg_randr);
	rr->opcode = r->major_opcode;
	rr->handle_event = (void *)handle_randr_event;
	rr->dc = dc;
	rr->extname = "randr";
	rr->free = NULL;
	rr->evbase = r->first_event;

	rr->cts = sr->config_timestamp;

	di_ref_object((void *)dc);

	HASH_ADD_KEYPTR(hh, dc->xext, rr->extname, strlen(rr->extname),
	                (struct di_xorg_ext *)rr);

	di_getter(rr, outputs, rr_outputs);
	di_getter(rr, modes, rr_modes);

	di_register_signal((void *)rr, "output-change", 1, (di_type_t[]){DI_TYPE_OBJECT});
	di_register_signal((void *)rr, "view-change", 1, (di_type_t[]){DI_TYPE_OBJECT});

	rr_select_input(rr,
	                XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE |
	                    XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE |
	                    XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE);

	free(r);
	return (void *)rr;
}
