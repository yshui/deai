/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai.h>
#include <helper.h>
#include <log.h>

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <xcb/xinput.h>

#include "xinput.h"
#include "xorg.h"

struct di_xorg_xinput {
	struct di_xorg_ext;

	xcb_input_event_mask_t ec;
	char mask[4];

	// XI_LASTEVENT is not defined in xcb,
	// but should be 26
	unsigned int listener_count[27];
};

struct di_xorg_xinput_device {
	struct di_object;

	int deviceid;
	struct di_xorg_connection *dc;
};

#define set_mask(a, m) (a)[(m) >> 3] |= (1 << ((m)&7))
#define clear_mask(a, m) (a)[(m) >> 3] &= ~(1 << ((m)&7))
#define get_mask(a, m) (a)[(m) >> 3] & (1 << ((m)&7))

static void di_xorg_xi_start_listen_for_event(struct di_xorg_xinput *xi, int ev) {
	di_getm(xi->dc->x->di, log);
	if (ev > 26) {
		di_log_va(logm, DI_LOG_ERROR, "invalid xi event number %d", ev);
		return;
	}

	xi->listener_count[ev]++;
	if (xi->listener_count[ev] > 1)
		return;

	auto scrn = screen_of_display(xi->dc->c, xi->dc->dflt_scrn);
	set_mask(xi->mask, ev);
	auto cookie =
	    xcb_input_xi_select_events_checked(xi->dc->c, scrn->root, 1, &xi->ec);
	auto e = xcb_request_check(xi->dc->c, cookie);
	if (e)
		di_log_va(logm, DI_LOG_ERROR, "select events failed\n");
}

static void di_xorg_xi_stop_listen_for_event(struct di_xorg_xinput *xi, int ev) {
	di_getm(xi->dc->x->di, log);
	if (ev > 26) {
		di_log_va(logm, DI_LOG_ERROR, "invalid xi event number %d", ev);
		return;
	}

	assert(xi->listener_count[ev] > 0);
	xi->listener_count[ev]--;
	if (xi->listener_count[ev] > 0)
		return;

	auto scrn = screen_of_display(xi->dc->c, xi->dc->dflt_scrn);
	clear_mask(xi->mask, ev);
	auto cookie =
	    xcb_input_xi_select_events_checked(xi->dc->c, scrn->root, 1, &xi->ec);
	auto e = xcb_request_check(xi->dc->c, cookie);
	if (e)
		di_log_va(logm, DI_LOG_ERROR, "select events failed\n");
}

static void di_xorg_xi_start_listen_for_hierarchy(struct di_xorg_xinput *xi) {
	di_xorg_xi_start_listen_for_event(xi, XCB_INPUT_HIERARCHY);
}

static void di_xorg_xi_stop_listen_for_hierarchy(struct di_xorg_xinput *xi) {
	di_xorg_xi_stop_listen_for_event(xi, XCB_INPUT_HIERARCHY);
}

static void di_xorg_free_xinput(struct di_xorg_ext *x) {
	// clear event mask when free
	di_getm(x->dc->x->di, log);
	struct di_xorg_xinput *xi = (void *)x;
	memset(xi->mask, 0, xi->ec.mask_len * 4);
	auto scrn = screen_of_display(xi->dc->c, xi->dc->dflt_scrn);
	xcb_input_xi_select_events_checked(xi->dc->c, scrn->root, 1, &xi->ec);

	auto cookie =
	    xcb_input_xi_select_events_checked(xi->dc->c, scrn->root, 1, &xi->ec);
	auto e = xcb_request_check(xi->dc->c, cookie);
	if (e)
		di_log_va(logm, DI_LOG_ERROR, "select events failed\n");
}

define_trivial_cleanup_t(xcb_input_xi_query_device_reply_t);

static xcb_input_xi_device_info_t *
xcb_input_get_device_info(xcb_connection_t *c, xcb_input_device_id_t deviceid,
                          xcb_input_xi_query_device_reply_t **rr) {
	*rr = NULL;
	auto r = xcb_input_xi_query_device_reply(
	    c, xcb_input_xi_query_device(c, deviceid), NULL);
	if (!r)
		return NULL;

	auto ri = xcb_input_xi_query_device_infos_iterator(r);
	xcb_input_xi_device_info_t *ret = NULL;
	for (; ri.rem; xcb_input_xi_device_info_next(&ri)) {
		auto info = ri.data;
		if (info->deviceid == deviceid) {
			ret = info;
			break;
		}
	}

	*rr = r;
	return ret;
}

static int xcb_intern_atom_checked(xcb_connection_t *c, const char *str,
                                   xcb_generic_error_t **e) {
	auto r = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, strlen(str), str), e);
	if (r) {
		auto ret = r->atom;
		free(r);
		return ret;
	}
	return -1;
}

static char *di_xorg_xinput_get_device_name(struct di_xorg_xinput_device *dev) {
	with_cleanup_t(xcb_input_xi_query_device_reply_t) rr;
	auto info = xcb_input_get_device_info(dev->dc->c, dev->deviceid, &rr);
	if (!info)
		return strdup("<unknown>");
	return strndup(xcb_input_xi_device_info_name(info),
	               xcb_input_xi_device_info_name_length(info));
}

static char *di_xorg_xinput_get_device_use(struct di_xorg_xinput_device *dev) {
	with_cleanup_t(xcb_input_xi_query_device_reply_t) rr;
	auto info = xcb_input_get_device_info(dev->dc->c, dev->deviceid, &rr);
	if (!info)
		return strdup("unknown");

	switch (info->type) {
	case XCB_INPUT_DEVICE_TYPE_MASTER_KEYBOARD: return strdup("master keyboard");
	case XCB_INPUT_DEVICE_TYPE_SLAVE_KEYBOARD: return strdup("keyboard");
	case XCB_INPUT_DEVICE_TYPE_MASTER_POINTER: return strdup("master pointer");
	case XCB_INPUT_DEVICE_TYPE_SLAVE_POINTER: return strdup("pointer");
	default: return strdup("unknown");
	}
}

define_trivial_cleanup_t(xcb_input_list_input_devices_reply_t);

#if 0
const char *possible_types[] = {
    "KEYBOARD",   "MOUSE",      "TABLET",    "TOUCHSCREEN", "TOUCHPAD",
    "BARCODE",    "BUTTONBOX",  "KNOB_BOX",  "ONE_KNOB",    "NINE_KNOB",
    "TRACKBALL",  "QUADRATURE", "ID_MODULE", "SPACEBALL",   "DATAGLOVE",
    "EYETRACKER", "CURSORKEYS", "FOOTMOUSE", "JOYSTICK", NULL
};
#endif

define_trivial_cleanup_t(xcb_get_atom_name_reply_t);

static char *di_xorg_xinput_get_device_type(struct di_xorg_xinput_device *dev) {
	with_cleanup_t(xcb_input_list_input_devices_reply_t) r =
	    xcb_input_list_input_devices_reply(
	        dev->dc->c, xcb_input_list_input_devices(dev->dc->c), NULL);

	auto di = xcb_input_list_input_devices_devices_iterator(r);
	for (; di.rem; xcb_input_device_info_next(&di))
		if (di.data->device_id == dev->deviceid)
			break;

	with_cleanup_t(xcb_get_atom_name_reply_t) ar = xcb_get_atom_name_reply(
	    dev->dc->c, xcb_get_atom_name(dev->dc->c, di.data->device_type), NULL);
	if (!ar) {
		// fprintf(stderr, "%d\n", di.data->device_type);
		return strdup("unknown");
	}

	char *ret =
	    strndup(xcb_get_atom_name_name(ar), xcb_get_atom_name_name_length(ar));
	for (int i = 0; ret[i]; i++)
		ret[i] = tolower(ret[i]);
	return ret;
}

static int di_xorg_xinput_get_device_id(struct di_object *o) {
	struct di_xorg_xinput_device *dev = (void *)o;
	return dev->deviceid;
}

define_trivial_cleanup_t(xcb_intern_atom_reply_t);
define_trivial_cleanup_t(xcb_input_xi_get_property_reply_t);
define_trivial_cleanup_t(char);
define_trivial_cleanup_t(xcb_input_xi_change_property_items_t);

static int di_xorg_xinput_set_prop(di_type_t *rtype, void **ret, unsigned int nargs,
                                   const di_type_t *atypes, const void *const *args,
                                   void *user_data) {
	struct di_xorg_xinput_device *dev = user_data;
	di_set_return(0);

	// At least 2 args, key and value
	if (nargs < 2)
		return -EINVAL;

	// Key must be a string
	if (atypes[0] != DI_TYPE_STRING)
		return -EINVAL;

	const char *key = *(const char **)args[0];
	with_cleanup_t(xcb_intern_atom_reply_t) prop_atom = xcb_intern_atom_reply(
	    dev->dc->c, xcb_intern_atom(dev->dc->c, 0, strlen(key), key), NULL);
	if (!prop_atom) {
		di_set_return(-EINVAL);
		return 0;
	}

	with_cleanup_t(xcb_intern_atom_reply_t) float_atom = xcb_intern_atom_reply(
	    dev->dc->c, xcb_intern_atom(dev->dc->c, 0, 5, "FLOAT"), NULL);

	with_cleanup_t(xcb_input_xi_get_property_reply_t) prop =
	    xcb_input_xi_get_property_reply(
	        dev->dc->c,
	        xcb_input_xi_get_property(dev->dc->c, dev->deviceid, 0,
	                                  prop_atom->atom, XCB_ATOM_ANY, 0, 0),
	        NULL);

	if (prop->type == XCB_ATOM_NONE) {
		di_set_return(-ENOENT);
		return 0;
	}

	with_cleanup_t(xcb_input_xi_change_property_items_t) item =
	    tmalloc(xcb_input_xi_change_property_items_t, nargs - 1);
	int step = prop->format / 8;
	with_cleanup_t(char) data = malloc(step * (nargs - 1));
	for (int i = 1; i < nargs; i++) {
		int64_t i64;
		float f;
		const char *str;
		xcb_intern_atom_reply_t *ir;
		void *curr = data + step * (i - 1);

		switch (atypes[i]) {
		case DI_TYPE_INT:
		case DI_TYPE_UINT:
			i64 = *(int64_t *)args[i];
			f = i64;
			break;
		case DI_TYPE_NINT:
		case DI_TYPE_NUINT:
			i64 = *(int *)args[i];
			f = i64;
			break;
		case DI_TYPE_FLOAT:
			f = *(float *)args[i];
			if (prop->type != float_atom->atom)
				return -EINVAL;
			break;
		case DI_TYPE_STRING:
			if (prop->type != XCB_ATOM_ATOM)
				return -EINVAL;
			str = *(const char **)args[i];
			ir = xcb_intern_atom_reply(
			    dev->dc->c,
			    xcb_intern_atom(dev->dc->c, 0, strlen(str), str), NULL);
			if (!ir)
				return -EINVAL;
			i64 = ir->atom;
			free(ir);
			break;
		default: return -EINVAL;
		}

		if (prop->type == XCB_ATOM_INTEGER ||
		    prop->type == XCB_ATOM_CARDINAL) {
			assert(atypes[i] != DI_TYPE_FLOAT);
			switch (prop->format) {
			case 8:
				*(int8_t *)curr = i64;
				item[i - 1].data8 = curr;
				break;
			case 16:
				*(int16_t *)curr = i64;
				item[i - 1].data16 = curr;
				break;
			case 32:
				*(int32_t *)curr = i64;
				item[i - 1].data32 = curr;
				break;
			}
		} else if (prop->type == float_atom->atom) {
			if (prop->format == 32)
				return -EINVAL;
			assert(atypes[i] != DI_TYPE_STRING);
			*(float *)curr = f;
			item[i - 1].data32 = curr;
		} else if (prop->type == XCB_ATOM_ATOM) {
			if (prop->format == 32)
				return -EINVAL;
			*(int32_t *)curr = i64;
			item[i - 1].data32 = curr;
		}
	}

	auto err = xcb_request_check(
	    dev->dc->c,
	    xcb_input_xi_change_property_aux_checked(
	        dev->dc->c, dev->deviceid, XCB_PROP_MODE_REPLACE, prop->format,
	        prop_atom->atom, prop->type, nargs - 1, item));

	if (err)
		di_set_return(-EBADE);

	return 0;
}

static struct di_object *
di_xorg_make_object_for_devid(struct di_xorg_connection *dc, int deviceid) {
	auto obj = di_new_object_with_type(struct di_xorg_xinput_device);

	obj->deviceid = deviceid;
	obj->dc = dc;

	auto tm = di_create_typed_method((di_fn_t)di_xorg_xinput_get_device_name,
	                                 "__get_name", DI_TYPE_STRING, 0);
	di_register_typed_method((void *)obj, tm);

	tm = di_create_typed_method((di_fn_t)di_xorg_xinput_get_device_use,
	                            "__get_use", DI_TYPE_STRING, 0);
	di_register_typed_method((void *)obj, tm);

	tm = di_create_typed_method((di_fn_t)di_xorg_xinput_get_device_id,
	                            "__get_id", DI_TYPE_NINT, 0);
	di_register_typed_method((void *)obj, tm);

	tm = di_create_typed_method((di_fn_t)di_xorg_xinput_get_device_type,
	                            "__get_type", DI_TYPE_STRING, 0);
	di_register_typed_method((void *)obj, tm);

	auto m = di_create_untyped_method(di_xorg_xinput_set_prop, "set_prop", obj);
	di_register_method((void *)obj, (void *)m);

	// const char *ty;
	// DI_GET((void *)obj, "name", ty);
	// fprintf(stderr, "NAME: %s", ty);
	return (void *)obj;
}

static struct di_array di_xorg_get_all_devices(struct di_xorg_xinput *xi) {
	with_cleanup_t(xcb_input_xi_query_device_reply_t) r =
	    xcb_input_xi_query_device_reply(
	        xi->dc->c, xcb_input_xi_query_device(xi->dc->c, 0), NULL);
	auto ri = xcb_input_xi_query_device_infos_iterator(r);

	int ndev = 0;
	for (; ri.rem; xcb_input_xi_device_info_next(&ri))
		ndev++;

	struct di_array ret;
	ret.length = ndev;
	ret.elem_type = DI_TYPE_OBJECT;
	if (ndev)
		ret.arr = tmalloc(void *, ndev);
	else
		ret.arr = NULL;

	struct di_object **arr = ret.arr;
	ri = xcb_input_xi_query_device_infos_iterator(r);
	for (int i = 0; i < ndev; i++) {
		arr[i] = di_xorg_make_object_for_devid(xi->dc, ri.data->deviceid);
		xcb_input_xi_device_info_next(&ri);
	}

	return ret;
}

static void
di_xorg_handle_xinput_event(struct di_xorg_xinput *xi, xcb_ge_generic_event_t *ev) {
	// di_getm(xi->dc->x->di, log);
	if (ev->event_type == XCB_INPUT_HIERARCHY) {
		xcb_input_hierarchy_event_t *hev = (void *)ev;
		auto hevi = xcb_input_hierarchy_infos_iterator(hev);
		for (; hevi.rem; xcb_input_hierarchy_info_next(&hevi)) {
			auto info = hevi.data;
			auto obj =
			    di_xorg_make_object_for_devid(xi->dc, info->deviceid);
			// di_log_va(log, DI_LOG_DEBUG, "hierarchy change %u %u\n",
			// info->deviceid, info->flags);
			if (info->flags & XCB_INPUT_HIERARCHY_MASK_SLAVE_ADDED)
				di_emit_signal_v((void *)xi, "new-device", obj);
			if (info->flags & XCB_INPUT_HIERARCHY_MASK_DEVICE_ENABLED)
				di_emit_signal_v((void *)xi, "device-enabled", obj);
			if (info->flags & XCB_INPUT_HIERARCHY_MASK_DEVICE_DISABLED)
				di_emit_signal_v((void *)xi, "device-disabled", obj);
			di_unref_object(obj);
		}
	}
}

struct di_object *di_xorg_get_xinput(struct di_object *o) {
	auto dc = (struct di_xorg_connection *)o;
	if (dc->xi) {
		di_ref_object((void *)dc->xi);
		return (void *)dc->xi;
	}

	char *extname = "XInputExtension";
	if (!xorg_has_extension(dc->c, extname))
		return NULL;

	auto cookie = xcb_query_extension(dc->c, strlen(extname), extname);
	auto r = xcb_query_extension_reply(dc->c, cookie, NULL);
	if (!r)
		return NULL;

	auto xi = di_new_object_with_type(struct di_xorg_xinput);
	xi->ec.mask_len = 1;        // 4 bytes unit
	xi->ec.deviceid = 0;        // alldevice
	xi->opcode = r->major_opcode;
	xi->handle_event = (void *)di_xorg_handle_xinput_event;
	dc->xi = (void *)xi;
	xi->dc = dc;
	xi->e = &dc->xi;

	di_ref_object((void *)dc);

	free(r);

	dc->xi->free = di_xorg_free_xinput;

	auto tm = di_create_typed_method((di_fn_t)di_xorg_free_sub, "__dtor",
	                                 DI_TYPE_VOID, 0);
	di_register_typed_method((void *)xi, tm);

	tm = di_create_typed_method((di_fn_t)di_xorg_xi_start_listen_for_hierarchy,
	                            "__add_listener_new-device", DI_TYPE_VOID, 0);
	di_register_typed_method((void *)xi, tm);

	tm = di_create_typed_method((di_fn_t)di_xorg_xi_stop_listen_for_hierarchy,
	                            "__del_listener_new-device", DI_TYPE_VOID, 0);
	di_register_typed_method((void *)xi, tm);

	tm = di_create_typed_method((di_fn_t)di_xorg_xi_start_listen_for_hierarchy,
	                            "__add_listener_device-enabled", DI_TYPE_VOID, 0);
	di_register_typed_method((void *)xi, tm);

	tm = di_create_typed_method((di_fn_t)di_xorg_xi_stop_listen_for_hierarchy,
	                            "__del_listener_device-enabled", DI_TYPE_VOID, 0);
	di_register_typed_method((void *)xi, tm);
	tm = di_create_typed_method((di_fn_t)di_xorg_xi_start_listen_for_hierarchy,
	                            "__add_listener_device-disabled", DI_TYPE_VOID,
	                            0);
	di_register_typed_method((void *)xi, tm);

	tm = di_create_typed_method((di_fn_t)di_xorg_xi_stop_listen_for_hierarchy,
	                            "__del_listener_device-disabled", DI_TYPE_VOID,
	                            0);
	di_register_typed_method((void *)xi, tm);

	tm = di_create_typed_method((di_fn_t)di_xorg_get_all_devices,
	                            "__get_devices", DI_TYPE_ARRAY, 0);
	di_register_typed_method((void *)xi, tm);

	di_register_signal((void *)xi, "new-device", 1, DI_TYPE_OBJECT);
	di_register_signal((void *)xi, "device-enabled", 1, DI_TYPE_OBJECT);
	di_register_signal((void *)xi, "device-disabled", 1, DI_TYPE_OBJECT);
	return (void *)dc->xi;
}
