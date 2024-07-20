/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai/builtins/log.h>
#include <deai/error.h>
#include <deai/helper.h>
#include <deai/type.h>

#include "xorg.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
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
	di_object;
	di_xorg_connection *dc;
};

struct di_xorg_output {
	di_object;

	xcb_randr_output_t id;
};

/// Type: deai.xorg.xorg.randr:OutputInfo
struct di_xorg_output_info {
	di_object;
	/// Width in millimeters
	///
	/// EXPORT: deai.plugin.xorg.randr:OutputInfo.mm_width: :unsigned
	unsigned int mm_width;
	/// Height in millimeters
	///
	/// EXPORT: deai.plugin.xorg.randr:OutputInfo.mm_height: :unsigned
	unsigned int mm_height;
	/// Connection
	///
	/// EXPORT: deai.plugin.xorg.randr:OutputInfo.connection: :integer
	///
	/// `Possible values
	/// <https://gitlab.freedesktop.org/xorg/proto/xorgproto/-/blob/09602b2/randrproto.txt#L2505-2510>`_
	int connection;
	/// Subpixel order
	///
	/// EXPORT: deai.plugin.xorg.randr:OutputInfo.subpixel_order: :integer
	int subpixel_order;
	/// Name
	///
	/// EXPORT: deai.plugin.xorg.randr:OutputInfo.name: :string
	const char *name;
	/// Number of preferred mode
	///
	/// EXPORT: deai.plugin.xorg.randr:OutputInfo.num_preferred: :unsigned
	unsigned int num_preferred;
};

/// Modes
///
/// EXPORT: deai.plugin.xorg.randr:OutputInfo.modes: [deai.plugin.xorg.randr:Mode]
void get_output_info_modes(di_object *);        // Unused function for documentation

// What xorg calls a crtc, we call a view.
//
// Who still has a CRT this day and age?
struct di_xorg_view {
	di_object;

	xcb_randr_crtc_t id;
	xcb_timestamp_t ts;
};

/// TYPE: deai.plugin.xorg.randr:Mode
struct di_xorg_mode {
	di_object;

	/// Mode id
	///
	/// EXPORT: deai.plugin.xorg.randr:Mode.id: :unsigned
	unsigned int id;
	/// Width
	///
	/// EXPORT: deai.plugin.xorg.randr:Mode.width: :unsigned
	unsigned int width;
	/// Height
	///
	/// EXPORT: deai.plugin.xorg.randr:Mode.height: :unsigned
	unsigned int height;
	/// Refresh rate
	///
	/// EXPORT: deai.plugin.xorg.randr:Mode.fps: :float
	double fps;
	/// Whether this mode is interlaced
	///
	/// EXPORT: deai.plugin.xorg.randr:Mode.interlaced: :bool
	bool interlaced;
	/// Whether this mode is double-scanned
	///
	/// EXPORT: deai.plugin.xorg.randr:Mode.double_scan: :bool
	bool double_scan;
};

/// TYPE: deai.plugin.xorg.randr:ViewConfig
struct di_xorg_view_config {
	di_object;
	/// EXPORT: deai.plugin.xorg.randr:ViewConfig.x: :integer
	///
	/// X offset
	///
	/// EXPORT: deai.plugin.xorg.randr:ViewConfig.y: :integer
	///
	/// Y offset
	int64_t x, y;
	/// EXPORT: deai.plugin.xorg.randr:ViewConfig.width: :integer
	///
	/// Width
	///
	/// EXPORT: deai.plugin.xorg.randr:ViewConfig.height: :integer
	///
	/// Height
	uint64_t width, height;
	/// EXPORT: deai.plugin.xorg.randr:ViewConfig.rotation: :unsigned
	///
	/// Rotation
	///
	/// 0, 1, 2, 3 means rotating 0, 90, 180, 270 degrees respectively.
	///
	/// EXPORT: deai.plugin.xorg.randr:ViewConfig.reflection: :unsigned
	///
	/// Reflection
	///
	/// Bit 1 means reflection along the X axis, bit 2 means the Y axis.
	///
	/// EXPORT: deai.plugin.xorg.randr:ViewConfig.outputs: [:unsigned]
	///
	/// Outputs
	uint64_t rotation, reflection;
};

define_trivial_cleanup(xcb_randr_get_screen_resources_reply_t);
define_trivial_cleanup(xcb_randr_get_screen_resources_current_reply_t);
define_trivial_cleanup(xcb_randr_get_output_info_reply_t);
define_trivial_cleanup(xcb_randr_get_crtc_info_reply_t);
define_trivial_cleanup(xcb_randr_query_output_property_reply_t);
define_trivial_cleanup(xcb_randr_get_output_property_reply_t);
define_trivial_cleanup(xcb_randr_set_crtc_config_reply_t);

static di_object *make_object_for_view(struct di_xorg_randr *rr, xcb_randr_crtc_t cid);

static void free_output_info(di_object *o) {
	auto i = (struct di_xorg_output_info *)o;
	free((char *)i->name);
}

/// Properties of an output
///
/// This functions as a dictionary of properties of an output. The keys are strings, and
/// the retrieved values are strings, arrays of strings, or arrays of integers.
///
/// Values from X server are converted as such:
///
///   - If the type is `ATOM`, then each of the values, which are atoms, are converted to
///     strings by looking up the atom name. The result is an array of strings.
///   - If the type is `INTEGER`, and the bit width is 8, then the values are treated as bytes,
///     and are returned as a single string. An example of such a property is the EDID.
///   - If the type is `INTEGER`, and the bit width is 16 or 32, then the values are returned
///     as an array of integers.
///
/// TYPE: deai.plugin.xorg.randr:OutputProps
struct di_xorg_output_props {
	// Unused object for documentation
	di_object;
};

static di_variant output_props_getter(di_object *this, di_string prop) {
	scoped_di_object *rr_obj;
	xcb_randr_output_t oid;
	di_get(this, "___randr", rr_obj);
	di_get(this, "___oid", oid);

	auto rr = (struct di_xorg_randr *)rr_obj;
	scopedp(di_xorg_connection) *dc = NULL;
	if (get_xorg_connection((struct di_xorg_ext *)rr, &dc) != 0) {
		return DI_VARIANT_INIT;
	}

	xcb_generic_error_t *e = NULL;
	auto atom = di_xorg_intern_atom(dc, prop, &e);
	if (e) {
		free(e);
		return DI_VARIANT_INIT;
	}

	scopedp(xcb_randr_get_output_property_reply_t) *r = xcb_randr_get_output_property_reply(
	    dc->c,
	    xcb_randr_get_output_property(dc->c, oid, atom, XCB_ATOM_ANY, 0, UINT_MAX, 0, 0), &e);
	if (e) {
		free(e);
		return DI_VARIANT_INIT;
	}
	if (r->type == XCB_ATOM_INTEGER && r->format == 8) {
		// Return bytes array as string
		di_variant ret = {
		    .value = calloc(1, sizeof(di_string)),
		    .type = DI_TYPE_STRING,
		};

		ret.value->string = di_string_ndup((char *)xcb_randr_get_output_property_data(r),
		                                   xcb_randr_get_output_property_data_length(r));
		return ret;
	}
	if (r->type == XCB_ATOM_INTEGER) {
		// Return array of integers
		di_variant ret = {
		    .value = calloc(1, sizeof(di_array)),
		    .type = DI_TYPE_ARRAY,
		};
		ret.value->array = (di_array){
		    .arr = calloc(r->num_items, sizeof(int)),
		    .length = r->num_items,
		    .elem_type = DI_TYPE_NINT,
		};
		if (!ret.value->array.arr) {
			return DI_VARIANT_INIT;
		}
		auto arr = (int *)ret.value->array.arr;
		auto data16 = (int16_t *)xcb_randr_get_output_property_data(r);
		auto data32 = (int32_t *)xcb_randr_get_output_property_data(r);
		for (int i = 0; i < r->num_items; i++) {
			switch (r->format) {
			case 16:
				arr[i] = data16[i];
				break;
			case 32:
				arr[i] = data32[i];
				break;
			default:
				assert(false);
				free(ret.value->array.arr);
				return DI_VARIANT_INIT;
			}
		}
		return ret;
	}
	if (r->type == XCB_ATOM_ATOM) {
		// Return array of strings
		di_variant ret = {
		    .value = calloc(1, sizeof(di_array)),
		    .type = DI_TYPE_ARRAY,
		};
		ret.value->array = (di_array){
		    .arr = calloc(r->num_items, sizeof(di_string)),
		    .length = r->num_items,
		    .elem_type = DI_TYPE_STRING,
		};
		if (!ret.value->array.arr) {
			return DI_VARIANT_INIT;
		}
		auto arr = (di_string *)ret.value->array.arr;
		auto atoms = (xcb_atom_t *)xcb_randr_get_output_property_data(r);
		for (int i = 0; i < r->num_items; i++) {
			arr[i] = di_clone_string(*di_xorg_get_atom_name(dc, atoms[i]));
		}
		return ret;
	}
	// Unknown?
	return DI_VARIANT_INIT;
}

/// Output properties
///
/// EXPORT: deai.plugin.xorg.randr:Output.props: deai.plugin.xorg.randr:OutputProps
static di_object *get_output_props_object(struct di_xorg_output *o) {
	auto ret = di_new_object_with_type(di_object);
	di_object *rr = NULL;
	DI_CHECK_OK(di_get(o, "___randr", rr));

	di_member(ret, "___randr", rr);
	di_member_clone(ret, "___oid", o->id);
	di_method(ret, "__get", output_props_getter, di_string);
	return ret;
}

static di_object *make_object_for_modes(struct di_xorg_randr *rr, xcb_randr_mode_info_t *m);
/// Output info
///
/// EXPORT: deai.plugin.xorg.randr:Output.info: deai.plugin.xorg.randr:OutputInfo
static di_object *get_output_info(struct di_xorg_output *o) {
	scopedp(di_xorg_connection) *dc = NULL;
	scoped_di_object *rr_obj = NULL;
	DI_CHECK_OK(di_get(o, "___randr", rr_obj));
	auto rr = (struct di_xorg_randr *)rr_obj;
	if (get_xorg_connection((struct di_xorg_ext *)rr, &dc) != 0) {
		return di_new_object_with_type(di_object);
	}

	scopedp(xcb_randr_get_output_info_reply_t) *r = xcb_randr_get_output_info_reply(
	    dc->c, xcb_randr_get_output_info(dc->c, o->id, rr->cts), NULL);

	if (!r || r->status != 0) {
		return di_new_object_with_type(di_object);
	}

	scopedp(xcb_randr_get_screen_resources_reply_t) *resource_reply =
	    xcb_randr_get_screen_resources_reply(
	        dc->c,
	        xcb_randr_get_screen_resources(dc->c, screen_of_display(dc->c, dc->dflt_scrn)->root),
	        NULL);
	if (!resource_reply) {
		return di_new_object_with_type(di_object);
	}

	auto ret = di_new_object_with_type2(struct di_xorg_output_info,
	                                    "deai.plugin.xorg.randr:OutputInfo");
	ret->name = strndup((char *)xcb_randr_get_output_info_name(r),
	                    xcb_randr_get_output_info_name_length(r));
	ret->connection = r->connection;
	ret->mm_width = r->mm_width;
	ret->mm_height = r->mm_height;
	ret->subpixel_order = r->subpixel_order;

	di_array modes = {
	    .elem_type = DI_TYPE_OBJECT,
	    .length = 0,
	};
	if (r->num_modes > 0) {
		modes.arr = tmalloc(void *, r->num_modes);
		auto arr = (di_object **)modes.arr;
		auto mode_infos = xcb_randr_get_screen_resources_modes(resource_reply);
		auto output_modes = xcb_randr_get_output_info_modes(r);
		for (int i = 0; i < r->num_modes; i++) {
			xcb_randr_mode_info_t *mode_info = NULL;
			for (int j = 0; j < resource_reply->num_modes; j++) {
				if (output_modes[i] == mode_infos[j].id) {
					mode_info = &mode_infos[j];
					break;
				}
			}
			if (mode_info != NULL) {
				arr[modes.length++] = make_object_for_modes(rr, mode_info);
			}
		}
	}
	ret->num_preferred = r->num_preferred;

	di_field(ret, connection);
	di_field(ret, mm_width);
	di_field(ret, mm_height);
	di_field(ret, name);
	di_field(ret, subpixel_order);
	di_field(ret, num_preferred);
	di_member(ret, "modes", modes);
	di_set_object_dtor((void *)ret, free_output_info);

	scoped_di_object *builtins = NULL;
	DI_CHECK_OK(di_get(dc, builtin_member_name, builtins));
	di_xorg_copy_from_builtins((di_object *)ret, "randr_output_info", builtins);

	return (di_object *)ret;
}

/// Output's current connected view
///
/// EXPORT: deai.plugin.xorg.randr:Output.current_view: deai.plugin.xorg.randr:View
///
/// A "view" is the portion of the overall Xorg screen that's displayed on this output.
static di_object *get_output_current_view(struct di_xorg_output *o) {
	scopedp(di_xorg_connection) *dc = NULL;
	scoped_di_object *rr_obj = NULL;
	DI_CHECK_OK(di_get(o, "___randr", rr_obj));
	auto rr = (struct di_xorg_randr *)rr_obj;
	if (get_xorg_connection((struct di_xorg_ext *)rr, &dc) != 0) {
		return di_new_object_with_type(di_object);
	}

	scopedp(xcb_randr_get_output_info_reply_t) *r = xcb_randr_get_output_info_reply(
	    dc->c, xcb_randr_get_output_info(dc->c, o->id, rr->cts), NULL);
	if (!r || r->status != 0 || r->crtc == 0) {
		return di_new_object_with_type(di_object);
	}
	return make_object_for_view(rr, r->crtc);
}

static di_array get_output_views(struct di_xorg_output *o) {
	scopedp(di_xorg_connection) *dc = NULL;
	scoped_di_object *rr_obj = NULL;
	DI_CHECK_OK(di_get(o, "___randr", rr_obj));
	auto rr = (struct di_xorg_randr *)rr_obj;
	if (get_xorg_connection((struct di_xorg_ext *)rr, &dc) != 0) {
		return DI_ARRAY_INIT;
	}

	scopedp(xcb_randr_get_output_info_reply_t) *r = xcb_randr_get_output_info_reply(
	    dc->c, xcb_randr_get_output_info(dc->c, o->id, rr->cts), NULL);
	if (!r || r->status != 0 || r->num_crtcs == 0) {
		return DI_ARRAY_INIT;
	}

	di_array ret = {
	    .length = r->num_crtcs,
	    .elem_type = DI_TYPE_OBJECT,
	    .arr = tmalloc(void *, r->num_crtcs),
	};
	auto arr = (di_object **)ret.arr;
	auto crtcs = xcb_randr_get_output_info_crtcs(r);
	for (int i = 0; i < r->num_crtcs; i++) {
		arr[i] = make_object_for_view(rr, crtcs[i]);
	}
	return ret;
}

/// View config
///
/// EXPORT: deai.plugin.xorg.randr:View.config: deai.plugin.xorg.randr:ViewConfig
///
/// The size, offset, and rotation settings of this view. This is read/write so you can,
/// say, change your screen resolution by changing the view config.
static di_object *get_view_config(struct di_xorg_view *v) {
	scopedp(di_xorg_connection) *dc = NULL;
	scoped_di_object *rr_obj = NULL;
	DI_CHECK_OK(di_get(v, "___randr", rr_obj));
	auto rr = (struct di_xorg_randr *)rr_obj;
	if (get_xorg_connection((struct di_xorg_ext *)rr, &dc) != 0) {
		return di_new_object_with_type(di_object);
	}

	scopedp(xcb_randr_get_crtc_info_reply_t) *cr = xcb_randr_get_crtc_info_reply(
	    dc->c, xcb_randr_get_crtc_info(dc->c, v->id, rr->cts), NULL);
	if (!cr || cr->status != 0) {
		return di_new_object_with_type(di_object);
	}

	auto ret = di_new_object_with_type2(struct di_xorg_view_config, "deai.plugin."
	                                                                "xorg.randr:"
	                                                                "ViewConfig");
	ret->x = cr->x;
	ret->y = cr->y;
	ret->width = cr->width;
	ret->height = cr->height;
	di_array outputs = DI_ARRAY_INIT;

	if (cr->num_outputs != 0) {
		auto rr_outputs = xcb_randr_get_crtc_info_outputs(cr);
		unsigned int *arr = outputs.arr = tmalloc(unsigned int, cr->num_outputs);
		outputs.length = cr->num_outputs;
		outputs.elem_type = DI_TYPE_NUINT;
		for (int i = 0; i < cr->num_outputs; i++) {
			arr[i] = rr_outputs[i];
		}
	}

	if (cr->rotation & XCB_RANDR_ROTATION_ROTATE_0) {
		ret->rotation = 0;
	} else if (cr->rotation & XCB_RANDR_ROTATION_ROTATE_90) {
		ret->rotation = 1;
	} else if (cr->rotation & XCB_RANDR_ROTATION_ROTATE_180) {
		ret->rotation = 2;
	} else if (cr->rotation & XCB_RANDR_ROTATION_ROTATE_270) {
		ret->rotation = 3;
	}

	if (cr->rotation & XCB_RANDR_ROTATION_REFLECT_X) {
		if (cr->rotation & XCB_RANDR_ROTATION_REFLECT_Y) {
			ret->reflection = 3;
		} else {
			ret->reflection = 1;
		}
	} else if (cr->rotation & XCB_RANDR_ROTATION_REFLECT_Y) {
		ret->reflection = 2;
	} else {
		ret->reflection = 0;
	}

	di_field(ret, x);
	di_field(ret, y);
	di_field(ret, width);
	di_field(ret, height);
	di_field(ret, rotation);
	di_field(ret, reflection);
	di_member(ret, "outputs", outputs);
	return (void *)ret;
}

static void set_view_config(struct di_xorg_view *v, di_object *cfg) {
	scopedp(di_xorg_connection) *dc = NULL;
	scoped_di_object *rr_obj = NULL;
	DI_CHECK_OK(di_get(v, "___randr", rr_obj));
	auto rr = (struct di_xorg_randr *)rr_obj;
	if (get_xorg_connection((struct di_xorg_ext *)rr, &dc) != 0) {
		return;
	}
	int x, y;
	xcb_randr_mode_t mode;
	int rotation, reflection;
	uint16_t rr_rot;
	scoped_di_array outputs = DI_ARRAY_INIT;

	di_gets(cfg, "x", x);
	di_gets(cfg, "y", y);
	di_gets(cfg, "mode", mode);
	di_gets(cfg, "rotation", rotation);
	di_gets(cfg, "reflection", reflection);
	bool outputs_set = di_get(cfg, "outputs", outputs) == 0;

	if (x < INT16_MIN || x > INT16_MAX) {
		return;
	}
	if (y < INT16_MIN || y > INT16_MAX) {
		return;
	}

	switch (rotation) {
	case 0:
		rr_rot = XCB_RANDR_ROTATION_ROTATE_0;
		break;
	case 1:
		rr_rot = XCB_RANDR_ROTATION_ROTATE_90;
		break;
	case 2:
		rr_rot = XCB_RANDR_ROTATION_ROTATE_180;
		break;
	case 3:
		rr_rot = XCB_RANDR_ROTATION_ROTATE_270;
		break;
	default:
		return;
	}
	if (reflection & 1) {
		rr_rot |= XCB_RANDR_ROTATION_REFLECT_X;
	}
	if (reflection & 2) {
		rr_rot |= XCB_RANDR_ROTATION_REFLECT_Y;
	}

	xcb_randr_output_t *rr_outputs = NULL;
	uint64_t n_outputs = 0;
	xcb_randr_output_t *outputs_to_free = NULL;
	if (outputs_set && outputs.length > 0) {
		outputs_to_free = rr_outputs = tmalloc(xcb_randr_output_t, outputs.length);
		n_outputs = outputs.length;
		const size_t elem_size = di_sizeof_type(outputs.elem_type);
		for (int i = 0; i < outputs.length; i++) {
			uint64_t id;
			void *ptr = (char *)outputs.arr + elem_size * i;
			if (di_type_conversion(outputs.elem_type, ptr, DI_TYPE_UINT, (void *)&id, true) != 0) {
				free(outputs_to_free);
				return;
			}
			rr_outputs[i] = id;
		}
	}
	// We might need to retry under race conditions, because Xorg sucks
	while (true) {
		scopedp(xcb_randr_get_crtc_info_reply_t) *cr = xcb_randr_get_crtc_info_reply(
		    dc->c, xcb_randr_get_crtc_info(dc->c, v->id, rr->cts), NULL);
		if (!cr || cr->status != 0) {
			break;
		}
		if (!outputs_set) {
			rr_outputs = xcb_randr_get_crtc_info_outputs(cr);
			n_outputs = cr->num_outputs;
		}

		scopedp(xcb_randr_set_crtc_config_reply_t) *scr = xcb_randr_set_crtc_config_reply(
		    dc->c,
		    xcb_randr_set_crtc_config(dc->c, v->id, cr->timestamp, rr->cts, x, y, mode,
		                              rr_rot, n_outputs, rr_outputs),
		    NULL);
		if (!scr) {
			break;
		}
		if (scr->status != XCB_RANDR_SET_CONFIG_INVALID_TIME) {
			break;
		}
	}
	free(outputs_to_free);
}

/// Backlight level
///
/// EXPORT: deai.plugin.xorg.randr:Output.backlight: :integer
///
/// Read/write property for getting/setting the screen backlight level. Check
/// :lua:attr:`max_backlight` for the largest possible level.
static int get_output_backlight(struct di_xorg_output *o) {
	scopedp(di_xorg_connection) *dc = NULL;
	scoped_di_object *rr = NULL;
	DI_CHECK_OK(di_get(o, "___randr", rr));
	if (get_xorg_connection((struct di_xorg_ext *)rr, &dc) != 0) {
		return 0;
	}

	xcb_generic_error_t *e;
	auto bklatom = di_xorg_intern_atom(dc, di_string_borrow_literal("Backlight"), &e);
	if (e) {
		free(e);
		return -1;
	}
	scopedp(xcb_randr_get_output_property_reply_t) *r = xcb_randr_get_output_property_reply(
	    dc->c,
	    xcb_randr_get_output_property(dc->c, o->id, bklatom, XCB_ATOM_ANY, 0, 4, 0, 0), NULL);

	if (!r || r->type != XCB_ATOM_INTEGER || r->num_items != 1 || r->format != 32) {
		return -1;
	}

	return *(int32_t *)xcb_randr_get_output_property_data(r);
}

static void set_output_backlight(struct di_xorg_output *o, int bkl) {
	scopedp(di_xorg_connection) *dc = NULL;
	scoped_di_object *rr = NULL;
	DI_CHECK_OK(di_get(o, "___randr", rr));
	if (get_xorg_connection((struct di_xorg_ext *)rr, &dc) != 0) {
		return;
	}

	xcb_generic_error_t *e;
	auto bklatom = di_xorg_intern_atom(dc, di_string_borrow_literal("Backlight"), &e);
	if (e) {
		free(e);
		return;
	}

	int32_t v = bkl;
	e = xcb_request_check(
	    dc->c, xcb_randr_change_output_property(dc->c, o->id, bklatom, XCB_ATOM_INTEGER,
	                                            32, XCB_PROP_MODE_REPLACE, 1, (void *)&v));
	if (e) {
		di_log_va(log_module, DI_LOG_ERROR, "Failed to set backlight");
	}
}

/// Max possible backlight level
///
/// EXPORT: deai.plugin.xorg.randr:Output.max_backlight: :integer
static int get_output_max_backlight(struct di_xorg_output *o) {
	scopedp(di_xorg_connection) *dc = NULL;
	scoped_di_object *rr = NULL;
	DI_CHECK_OK(di_get(o, "___randr", rr));
	if (get_xorg_connection((struct di_xorg_ext *)rr, &dc) != 0) {
		return -1;
	}

	xcb_generic_error_t *e;
	auto bklatom = di_xorg_intern_atom(dc, di_string_borrow_literal("Backlight"), &e);
	if (e) {
		free(e);
		return -1;
	}

	scopedp(xcb_randr_query_output_property_reply_t) *r = xcb_randr_query_output_property_reply(
	    dc->c, xcb_randr_query_output_property(dc->c, o->id, bklatom), NULL);
	if (!r) {
		return -1;
	}

	auto vv = xcb_randr_query_output_property_valid_values(r);
	auto vvl = xcb_randr_query_output_property_valid_values_length(r);
	if (vvl != 2) {
		return -1;
	}
	return vv[1];
}

static di_object *make_object_for_output(struct di_xorg_randr *rr, xcb_randr_output_t oid) {
	DI_CHECK(di_has_member(rr, XORG_CONNECTION_MEMBER));

	auto obj =
	    di_new_object_with_type2(struct di_xorg_output, "deai.plugin.xorg.randr:Output");
	obj->id = oid;
	di_field(obj, id);
	di_getter(obj, current_view, get_output_current_view);
	di_getter(obj, views, get_output_views);
	di_getter(obj, info, get_output_info);
	di_getter_setter(obj, backlight, get_output_backlight, set_output_backlight);
	di_getter(obj, max_backlight, get_output_max_backlight);
	di_getter(obj, props, get_output_props_object);

	di_member_clone(obj, "___randr", (di_object *)rr);
	return (void *)obj;
}

/// View outputs
///
/// EXPORT: deai.plugin.xorg.randr:View.outputs: [deai.plugin.xorg.randr:Output]
///
/// A list of outputs that is connected to this view.
static di_array get_view_outputs(struct di_xorg_view *v) {
	di_array ret = DI_ARRAY_INIT;
	scopedp(di_xorg_connection) *dc = NULL;
	scoped_di_object *rr_obj = NULL;
	DI_CHECK_OK(di_get(v, "___randr", rr_obj));
	auto rr = (struct di_xorg_randr *)rr_obj;
	if (get_xorg_connection((struct di_xorg_ext *)rr, &dc) != 0) {
		return ret;
	}

	scopedp(xcb_randr_get_crtc_info_reply_t) *r = xcb_randr_get_crtc_info_reply(
	    dc->c, xcb_randr_get_crtc_info(dc->c, v->id, rr->cts), NULL);

	auto outputs = xcb_randr_get_crtc_info_outputs(r);
	ret.elem_type = DI_TYPE_OBJECT;
	ret.length = xcb_randr_get_crtc_info_outputs_length(r);
	if (ret.length == 0) {
		return ret;
	}

	void **arr = ret.arr = tmalloc(void *, ret.length);
	for (int i = 0; i < ret.length; i++) {
		arr[i] = make_object_for_output(rr, outputs[i]);
	}

	return ret;
}

static di_object *make_object_for_view(struct di_xorg_randr *rr, xcb_randr_crtc_t cid) {
	DI_CHECK(di_has_member(rr, XORG_CONNECTION_MEMBER));

	auto obj =
	    di_new_object_with_type2(struct di_xorg_view, "deai.plugin.xorg.randr:View");
	obj->id = cid;

	di_getter(obj, outputs, get_view_outputs);
	di_field(obj, id);
	di_getter_setter(obj, config, get_view_config, set_view_config);

	di_member_clone(obj, "___randr", (di_object *)rr);

	return (void *)obj;
}

/// SIGNAL: deai.plugin.xorg:RandrExt.output-change(output) An output's configuration changed
///
/// Arguments:
///
/// - output(deai.plugin.xorg.randr:Output) the output that changed.
///
/// SIGNAL: deai.plugin.xorg:RandrExt.view-change(view) A view's configuration changed
///
/// Arguments:
///
/// - view(deai.plugin.xorg.randr:View) the view that changed.
static int handle_randr_event(struct di_xorg_ext *ext, xcb_generic_event_t *ev) {
	struct di_xorg_randr *rr = (void *)ext;
	if (ev->response_type == rr->evbase) {
		xcb_randr_screen_change_notify_event_t *sev = (void *)ev;
		rr->cts = sev->config_timestamp;
	} else if (ev->response_type == rr->evbase + 1) {
		xcb_randr_notify_event_t *rev = (void *)ev;
		di_object *o;
		if (rev->subCode == XCB_RANDR_NOTIFY_OUTPUT_CHANGE) {
			o = make_object_for_output(rr, rev->u.oc.output);
			di_emit(ext, "output-change", o);
			rr->cts = rev->u.oc.config_timestamp;
			di_unref_object(o);
		} else if (rev->subCode == XCB_RANDR_NOTIFY_CRTC_CHANGE) {
			o = make_object_for_view(rr, rev->u.cc.crtc);
			di_emit(ext, "view-change", o);
			di_unref_object(o);
		}
	} else {
		return 1;
	}
	return 0;
}

static inline void rr_select_input(struct di_xorg_randr *rr, uint16_t mask) {
	scopedp(di_xorg_connection) *dc = NULL;
	if (get_xorg_connection((struct di_xorg_ext *)rr, &dc) != 0) {
		return;
	}

	if (dc->c == NULL) {
		return;
	}

	auto scrn = screen_of_display(dc->c, dc->dflt_scrn);
	auto e = xcb_request_check(dc->c, xcb_randr_select_input(dc->c, scrn->root, mask));

	if (e) {
		di_log_va(log_module, DI_LOG_ERROR, "randr select input failed\n");
	}
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

#define event_funcs(mask_name, name)                                                     \
	static void enable_##name##_event(struct di_xorg_randr *rr) {                        \
		enable_randr_event(rr, XCB_RANDR_NOTIFY_MASK_##mask_name);                       \
	}                                                                                    \
                                                                                         \
	static void disable_##name##_event(struct di_xorg_randr *rr) {                       \
		disable_randr_event(rr, XCB_RANDR_NOTIFY_MASK_##mask_name);                      \
	}
#endif

// event_funcs(OUTPUT_CHANGE, output_change);

/// Outputs
///
/// EXPORT: deai.plugin.xorg:RandrExt.outputs: [deai.plugin.xorg.randr:Output]
///
/// Generally, outputs are what we would call monitors.
static di_array rr_outputs(struct di_xorg_randr *rr) {
	di_array ret = DI_ARRAY_INIT;
	scopedp(di_xorg_connection) *dc = NULL;
	if (get_xorg_connection((struct di_xorg_ext *)rr, &dc) != 0) {
		return ret;
	}

	auto scrn = screen_of_display(dc->c, dc->dflt_scrn);
	scopedp(xcb_randr_get_screen_resources_current_reply_t) *sr =
	    xcb_randr_get_screen_resources_current_reply(
	        dc->c, xcb_randr_get_screen_resources_current(dc->c, scrn->root), NULL);
	if (!sr) {
		return ret;
	}

	ret.length = xcb_randr_get_screen_resources_current_outputs_length(sr);
	if (ret.length == 0) {
		return ret;
	}
	ret.elem_type = DI_TYPE_OBJECT;
	auto os = xcb_randr_get_screen_resources_current_outputs(sr);
	void **arr = ret.arr = tmalloc(void *, ret.length);
	for (int i = 0; i < ret.length; i++) {
		arr[i] = make_object_for_output(rr, os[i]);
	}

	return ret;
}

static di_object *make_object_for_modes(struct di_xorg_randr *rr, xcb_randr_mode_info_t *m) {
	auto o = di_new_object_with_type2(struct di_xorg_mode, "deai.plugin.xorg.randr:Mode");
	o->id = m->id;
	o->width = m->width;
	o->height = m->height;

	double vtotal = m->vtotal;
	if (m->mode_flags & XCB_RANDR_MODE_FLAG_INTERLACE) {
		o->interlaced = true;
		vtotal /= 2.0;
	}
	if (m->mode_flags & XCB_RANDR_MODE_FLAG_DOUBLE_SCAN) {
		o->double_scan = true;
		vtotal *= 2.0;
	}
	if (m->htotal != 0 && vtotal != 0) {
		o->fps = (double)m->dot_clock / (m->htotal * vtotal);
	} else {
		o->fps = NAN;
	}

	di_field(o, interlaced);
	di_field(o, double_scan);
	di_field(o, width);
	di_field(o, height);
	di_field(o, id);
	di_field(o, fps);

	return (void *)o;
}

/// Available modes from RandR
///
/// EXPORT: deai.plugin.xorg:RandrExt.modes: [deai.plugin.xorg.randr:Mode]
static di_array rr_modes(struct di_xorg_randr *rr) {
	di_array ret = DI_ARRAY_INIT;
	scopedp(di_xorg_connection) *dc = NULL;
	if (get_xorg_connection((struct di_xorg_ext *)rr, &dc) != 0) {
		return ret;
	}

	auto scrn = screen_of_display(dc->c, dc->dflt_scrn);
	scopedp(xcb_randr_get_screen_resources_current_reply_t) *sr =
	    xcb_randr_get_screen_resources_current_reply(
	        dc->c, xcb_randr_get_screen_resources_current(dc->c, scrn->root), NULL);
	if (!sr) {
		return ret;
	}

	ret.length = xcb_randr_get_screen_resources_current_modes_length(sr);
	if (ret.length == 0) {
		return ret;
	}

	ret.elem_type = DI_TYPE_OBJECT;

	auto mi = xcb_randr_get_screen_resources_current_modes_iterator(sr);
	di_object **arr = ret.arr = tmalloc(di_object *, ret.length);
	for (int i = 0; mi.rem; xcb_randr_mode_info_next(&mi), i++) {
		arr[i] = make_object_for_modes(rr, mi.data);
	}

	return ret;
}

static void rr_set_screen_size(di_object *rr, di_object *size) {
	scopedp(di_xorg_connection) *dc = NULL;
	if (get_xorg_connection((struct di_xorg_ext *)rr, &dc) != 0) {
		return;
	}

	auto scrn = screen_of_display(dc->c, dc->dflt_scrn);
	int width, height, width_mm, height_mm;
	di_gets(size, "width", width);
	di_gets(size, "height", height);
	di_gets(size, "mm_width", width_mm);
	di_gets(size, "mm_height", height_mm);

	xcb_randr_set_screen_size(dc->c, scrn->root, width, height, width_mm, height_mm);
	xcb_flush(dc->c);
}

static di_object *rr_screen_resources(di_object *rr) {
	scopedp(di_xorg_connection) *dc = NULL;
	if (get_xorg_connection((struct di_xorg_ext *)rr, &dc) != 0) {
		di_throw(di_new_error("no X connection"));
	}

	auto scrn = screen_of_display(dc->c, dc->dflt_scrn);
	scopedp(xcb_randr_get_screen_resources_reply_t) *sr = xcb_randr_get_screen_resources_reply(
	    dc->c, xcb_randr_get_screen_resources(dc->c, scrn->root), NULL);
	if (!sr) {
		di_throw(di_new_error("failed to get screen resources"));
	}

	auto ret = di_new_object_with_type(di_object);
	di_set_type(ret, "deai.plugin.xorg.randr:ScreenResources");

	di_array modes = DI_ARRAY_INIT;
	modes.length = xcb_randr_get_screen_resources_modes_length(sr);
	if (modes.length != 0) {
		modes.elem_type = DI_TYPE_OBJECT;
		auto rr_modes = xcb_randr_get_screen_resources_modes(sr);
		di_object **arr = modes.arr = tmalloc(di_object *, modes.length);
		for (int i = 0; i < modes.length; i++) {
			arr[i] = make_object_for_modes((struct di_xorg_randr *)rr, &rr_modes[i]);
		}
		di_member(ret, "modes", modes);
	}

	di_array outputs = DI_ARRAY_INIT;
	outputs.length = xcb_randr_get_screen_resources_outputs_length(sr);
	if (outputs.length != 0) {
		outputs.elem_type = DI_TYPE_OBJECT;
		auto rr_outputs = xcb_randr_get_screen_resources_outputs(sr);
		di_object **arr = outputs.arr = tmalloc(di_object *, outputs.length);
		for (int i = 0; i < outputs.length; i++) {
			arr[i] = make_object_for_output((struct di_xorg_randr *)rr, rr_outputs[i]);
		}
		di_member(ret, "outputs", outputs);
	}

	di_array views = DI_ARRAY_INIT;
	views.length = xcb_randr_get_screen_resources_crtcs_length(sr);
	if (views.length != 0) {
		views.elem_type = DI_TYPE_OBJECT;
		auto rr_views = xcb_randr_get_screen_resources_crtcs(sr);
		di_object **arr = views.arr = tmalloc(di_object *, views.length);
		for (int i = 0; i < views.length; i++) {
			arr[i] = make_object_for_view((struct di_xorg_randr *)rr, rr_views[i]);
		}
		di_member(ret, "views", views);
	}

	return ret;
}

static void free_randr(di_object *x) {
	auto rr = (struct di_xorg_randr *)x;
	rr_select_input(rr, 0);
}

/// RandR extension
///
/// EXPORT: deai.plugin.xorg:Connection.randr: deai.plugin.xorg:RandrExt
struct di_xorg_ext *new_randr(di_xorg_connection *dc) {
	char *extname = "RANDR";
	if (!xorg_has_extension(dc->c, extname)) {
		return NULL;
	}

	auto cookie = xcb_query_extension(dc->c, strlen(extname), extname);
	auto r = xcb_query_extension_reply(dc->c, cookie, NULL);
	if (!r) {
		return NULL;
	}

	auto scrn = screen_of_display(dc->c, dc->dflt_scrn);
	scopedp(xcb_randr_get_screen_resources_current_reply_t) *sr =
	    xcb_randr_get_screen_resources_current_reply(
	        dc->c, xcb_randr_get_screen_resources_current(dc->c, scrn->root), NULL);
	if (!sr) {
		return NULL;
	}

	scoped_di_object *builtins = NULL;
	DI_CHECK_OK(di_get(dc, builtin_member_name, builtins));

	auto rr = di_new_object_with_type2(struct di_xorg_randr, "deai.plugin.xorg:"
	                                                         "RandrExt");
	rr->opcode = r->major_opcode;
	rr->handle_event = (void *)handle_randr_event;
	rr->extname = "randr";
	rr->evbase = r->first_event;

	rr->cts = sr->config_timestamp;

	di_getter(rr, outputs, rr_outputs);
	di_getter(rr, modes, rr_modes);
	di_getter(rr, screen_resources, rr_screen_resources);
	di_setter(rr, screen_size, rr_set_screen_size, di_object *);

	di_signal_setter_deleter_with_signal_name(
	    rr, "output-change", di_xorg_ext_signal_setter, di_xorg_ext_signal_deleter);
	di_signal_setter_deleter_with_signal_name(
	    rr, "view-change", di_xorg_ext_signal_setter, di_xorg_ext_signal_deleter);
	di_set_object_dtor((void *)rr, (void *)free_randr);

	di_xorg_copy_from_builtins((di_object *)rr, "randr", builtins);

	save_xorg_connection((struct di_xorg_ext *)rr, dc);

	rr_select_input(rr, XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE | XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE |
	                        XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE);

	free(r);
	return (void *)rr;
}
