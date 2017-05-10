/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <helper.h>
#include <log.h>

#include "randr.h"

#include <assert.h>
#include <stdio.h>
#include <xcb/randr.h>

struct di_xorg_randr {
	struct di_xorg_ext;

	uint16_t notify_mask;
	unsigned listener_count[7];
};

static void handle_randr_event(struct di_xorg_ext *ext, xcb_ge_generic_event_t *ev) {
}

static inline void randr_select_input(struct di_xorg_randr *rr) {
	auto scrn = screen_of_display(rr->dc->c, rr->dc->dflt_scrn);
	auto e = xcb_request_check(
	    rr->dc->c, xcb_randr_select_input(rr->dc->c, scrn->root, rr->notify_mask));

	di_getm(rr->dc->x->di, log);
	if (e)
		di_log_va(logm, DI_LOG_ERROR, "randr select input failed\n");
}

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

event_funcs(OUTPUT_CHANGE, output_change);

struct di_xorg_outputs {
	struct di_object;
	struct di_xorg_connection *dc;
};

struct di_xorg_output {
	struct di_object;
	struct di_xorg_connection *dc;

	xcb_randr_output_t oid;
	xcb_timestamp_t ts;
};

define_trivial_cleanup_t(xcb_randr_get_screen_resources_reply_t);
define_trivial_cleanup_t(xcb_randr_get_output_info_reply_t);
define_trivial_cleanup_t(xcb_randr_get_crtc_info_reply_t);

static xcb_randr_get_crtc_info_reply_t *
get_crtc_for_output(xcb_connection_t *c, xcb_randr_output_t oid, xcb_timestamp_t ts) {
	with_cleanup_t(xcb_randr_get_output_info_reply_t) r =
	    xcb_randr_get_output_info_reply(c, xcb_randr_get_output_info(c, oid, ts),
	                                    NULL);
	if (!r || r->status != 0)
		return NULL;

	auto cr = xcb_randr_get_crtc_info_reply(
	    c, xcb_randr_get_crtc_info(c, r->crtc, ts), NULL);
	if (!cr)
		return NULL;
	if (cr->status != 0) {
		free(cr);
		return NULL;
	}
	return cr;
}

struct di_xorg_rr_info {
	struct di_object;
	int64_t x, y;
	uint64_t width, height;
	uint64_t rotation, reflection;
};

static struct di_object *di_xorg_rr_get_info(struct di_xorg_output *o) {
	auto info = get_crtc_for_output(o->dc->c, o->oid, o->ts);
	if (!info)
		return NULL;

	auto ret = di_new_object_with_type(struct di_xorg_rr_info);
	ret->x = info->x;
	ret->y = info->y;
	ret->width = info->width;
	ret->height = info->height;
	if (info->rotation & XCB_RANDR_ROTATION_ROTATE_0)
		ret->rotation = 0;
	else if (info->rotation & XCB_RANDR_ROTATION_ROTATE_90)
		ret->rotation = 1;
	else if (info->rotation & XCB_RANDR_ROTATION_ROTATE_180)
		ret->rotation = 2;
	else if (info->rotation & XCB_RANDR_ROTATION_ROTATE_270)
		ret->rotation = 3;

	if (info->rotation & XCB_RANDR_ROTATION_REFLECT_X) {
		if (info->rotation & XCB_RANDR_ROTATION_REFLECT_Y)
			ret->reflection = 3;
		else
			ret->reflection = 1;
	} else if (info->rotation & XCB_RANDR_ROTATION_REFLECT_Y)
		ret->reflection = 2;
	else
		ret->reflection = 0;
	free(info);

	di_field(ret, x);
	di_field(ret, y);
	di_field(ret, height);
	di_field(ret, width);
	di_field(ret, rotation);
	di_field(ret, reflection);
	return (void *)ret;
}

static struct di_object *
make_object_for_output(struct di_xorg_connection *dc, xcb_randr_output_t oid,
                       xcb_timestamp_t ts) {
	auto obj = di_new_object_with_type(struct di_xorg_output);
	obj->dc = dc;
	obj->oid = oid;
	obj->ts = ts;
	di_register_r_property((void *)obj, "info", (di_fn_t)di_xorg_rr_get_info,
	                       DI_TYPE_OBJECT);

	return (void *)obj;
}

static struct di_xorg_output *
di_xorg_rr_get_output(struct di_xorg_outputs *o, const char *name) {
	auto scrn = screen_of_display(o->dc->c, o->dc->dflt_scrn);
	with_cleanup_t(xcb_randr_get_screen_resources_reply_t) sr =
	    xcb_randr_get_screen_resources_reply(
	        o->dc->c, xcb_randr_get_screen_resources(o->dc->c, scrn->root), NULL);
	if (!sr)
		return NULL;

	fprintf(stderr, "%d\n", sr->config_timestamp);
	fprintf(stderr, "%d\n", sr->timestamp);
	int olen = xcb_randr_get_screen_resources_outputs_length(sr);
	auto os = xcb_randr_get_screen_resources_outputs(sr);
	for (int i = 0; i < olen; i++) {
		with_cleanup_t(xcb_randr_get_output_info_reply_t) r =
		    xcb_randr_get_output_info_reply(
		        o->dc->c, xcb_randr_get_output_info(o->dc->c, os[i],
		                                            sr->config_timestamp),
		        NULL);
		if (!r)
			return NULL;
		char *oname = (void *)xcb_randr_get_output_info_name(r);
		auto onlen = xcb_randr_get_output_info_name_length(r);
		fprintf(stderr, "%d\n", r->timestamp);
		if (strncmp(oname, name, onlen) == 0)
			return (void *)make_object_for_output(o->dc, os[i],
			                                      sr->config_timestamp);
	}
	return NULL;
}

static struct di_xorg_outputs *di_xorg_rr_outputs(struct di_xorg_randr *rr) {
	auto o = di_new_object_with_type(struct di_xorg_outputs);
	o->dc = rr->dc;

	di_register_typed_method(
	    (void *)o,
	    di_create_typed_method((di_fn_t)di_xorg_rr_get_output, "__get",
	                           DI_TYPE_OBJECT, 1, DI_TYPE_STRING));

	return o;
}

struct di_xorg_ext *di_xorg_new_randr(struct di_xorg_connection *dc) {
	char *extname = "RANDR";
	if (!xorg_has_extension(dc->c, extname))
		return NULL;

	auto cookie = xcb_query_extension(dc->c, strlen(extname), extname);
	auto r = xcb_query_extension_reply(dc->c, cookie, NULL);
	if (!r)
		return NULL;

	auto rr = di_new_object_with_type(struct di_xorg_randr);
	rr->opcode = r->major_opcode;
	rr->handle_event = (void *)handle_randr_event;
	rr->dc = dc;
	rr->extname = "randr";
	rr->free = NULL;

	di_ref_object((void *)dc);

	HASH_ADD_KEYPTR(hh, dc->xext, rr->extname, strlen(rr->extname),
	                (struct di_xorg_ext *)rr);

	di_register_typed_method(
	    (void *)rr, di_create_typed_method((di_fn_t)di_xorg_rr_outputs,
	                                       "__get_outputs", DI_TYPE_OBJECT, 0));


	di_signal_handler(rr, "output-change", enable_output_change_event,
	                  disable_output_change_event);
	di_register_signal((void *)rr, "output-change", 1, DI_TYPE_OBJECT);

	free(r);
	return (void *)rr;
}
