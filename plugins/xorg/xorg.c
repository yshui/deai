/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai/builtins/event.h>
#include <deai/builtins/log.h>
#include <deai/deai.h>
#include <deai/helper.h>

#include <assert.h>
#include <stdio.h>
#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon.h>

#include "uthash.h"
#include "utils.h"

#include "xorg.h"

struct di_atom_entry {
	struct di_string name;
	xcb_atom_t atom;

	UT_hash_handle hh, hh2;
};

define_trivial_cleanup_t(xcb_generic_error_t);

static void di_xorg_free_sub(struct di_xorg_ext *x) {
	if (x->dc) {
		HASH_DEL(x->dc->xext, x);
		di_unref_object((void *)x->dc);
		x->dc = NULL;

		if (x->free) {
			// HACKY, x->free might free x.
			// TODO(yshui) hold a weak reference to x
			x->free(x);
		}
	}
}

/// Disconnect from the X server
///
/// EXPORT: deai.plugin.xorg:Connection.disconnect(), :void
///
/// Disconnecting from the X server will stop all related event sources.
/// You should stop using the Connection object after you have called disconnect.
static void xorg_disconnect(struct di_xorg_connection *xc) {
	if (!di_has_member(xc, "__xcb_fd_event")) {
		return;
	}

	if (xc->xkb_ctx) {
		xkb_context_unref(xc->xkb_ctx);
	}

	// free_sub might need the connection, don't disconnect now
	struct di_xorg_ext *ext, *text;
	HASH_ITER (hh, xc->xext, ext, text) {
		// free the extension objects
		di_finalize_object((void *)ext);
	}
	DI_CHECK(xc->c != NULL);
	xcb_disconnect(xc->c);
	xc->x = NULL;
	xc->c = NULL;

	struct di_atom_entry *ae, *tae;
	HASH_ITER (hh, xc->a_byatom, ae, tae) {
		HASH_DEL(xc->a_byatom, ae);
		HASH_DELETE(hh2, xc->a_byname, ae);
		di_free_string(ae->name);
		free(ae);
	}
}

static void di_xorg_ioev(struct di_weak_object *weak) {
	// di_get_log(dc->x->di);
	// di_log_va((void *)log, DI_LOG_DEBUG, "xcb ioev\n");

	di_object_with_cleanup dc_obj = di_upgrade_weak_ref(weak);
	auto dc = (struct di_xorg_connection *)dc_obj;

	DI_CHECK(dc != NULL, "got ioev events but the listener has died");
	xcb_generic_event_t *ev;

	while ((ev = xcb_poll_for_event(dc->c))) {
		// handle event

		struct di_xorg_ext *ex, *tmp;
		HASH_ITER (hh, dc->xext, ex, tmp) {
			int status = ex->handle_event(ex, ev);
			if (status != 1) {
				break;
			}
		}
		free(ev);
	}

	if (xcb_connection_has_error(dc->c)) {
		di_emit(dc, "connection-error");
		di_finalize_object((struct di_object *)dc);
	}
}

const struct di_string *di_xorg_get_atom_name(struct di_xorg_connection *xc, xcb_atom_t atom) {
	struct di_atom_entry *ae = NULL;
	HASH_FIND(hh, xc->a_byatom, &atom, sizeof(atom), ae);
	if (ae) {
		return &ae->name;
	}

	auto r = xcb_get_atom_name_reply(xc->c, xcb_get_atom_name(xc->c, atom), NULL);
	if (!r) {
		return NULL;
	}

	ae = tmalloc(struct di_atom_entry, 1);
	ae->name = di_string_ndup(xcb_get_atom_name_name(r), xcb_get_atom_name_name_length(r));
	ae->atom = atom;
	free(r);

	HASH_ADD(hh, xc->a_byatom, atom, sizeof(xcb_atom_t), ae);
	HASH_ADD_KEYPTR(hh2, xc->a_byname, ae->name.data, ae->name.length, ae);

	return &ae->name;
}

xcb_atom_t di_xorg_intern_atom(struct di_xorg_connection *xc, struct di_string name,
                               xcb_generic_error_t **e) {
	di_mgetm(xc->x, log, 0);
	struct di_atom_entry *ae = NULL;
	*e = NULL;

	HASH_FIND(hh2, xc->a_byname, name.data, name.length, ae);
	if (ae) {
		return ae->atom;
	}

	auto r = xcb_intern_atom_reply(
	    xc->c, xcb_intern_atom(xc->c, 0, name.length, name.data), e);
	if (!r) {
		di_log_va(logm, DI_LOG_ERROR, "Cannot intern atom");
		return 0;
	}

	ae = tmalloc(struct di_atom_entry, 1);
	ae->atom = r->atom;
	ae->name = di_clone_string(name);
	free(r);

	HASH_ADD(hh, xc->a_byatom, atom, sizeof(xcb_atom_t), ae);
	HASH_ADD_KEYPTR(hh2, xc->a_byname, ae->name.data, ae->name.length, ae);

	return ae->atom;
}

/// The X resource database (xrdb)
///
/// EXPORT: deai.plugin.xorg:Connection.xrdb, :string
///
/// This property corresponds to the xrdb, which is usually set with the command line
/// tool with the same name. Assigning to this property updates the xrdb.
static struct di_string di_xorg_get_resource(struct di_xorg_connection *xc) {
	auto scrn = screen_of_display(xc->c, xc->dflt_scrn);
	auto r = xcb_get_property_reply(
	    xc->c,
	    xcb_get_property(xc->c, 0, scrn->root, XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_ANY, 0, 0),
	    NULL);
	if (!r) {
		return DI_STRING_INIT;
	}

	auto real_size = r->bytes_after;
	free(r);

	r = xcb_get_property_reply(xc->c,
	                           xcb_get_property(xc->c, 0, scrn->root, XCB_ATOM_RESOURCE_MANAGER,
	                                            XCB_ATOM_ANY, 0, real_size),
	                           NULL);
	if (!r) {
		return DI_STRING_INIT;
	}

	auto ret = di_string_ndup(xcb_get_property_value(r), xcb_get_property_value_length(r));
	free(r);

	return ret;
}

static void di_xorg_set_resource(struct di_xorg_connection *xc, struct di_string rdb) {
	auto scrn = screen_of_display(xc->c, xc->dflt_scrn);
	with_cleanup_t(xcb_generic_error_t) e = xcb_request_check(
	    xc->c, xcb_change_property(xc->c, XCB_PROP_MODE_REPLACE, scrn->root,
	                               XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, 8,
	                               rdb.length, rdb.data));
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

static struct di_variant di_xorg_get_ext(struct di_xorg_connection *xc, struct di_string name) {
	struct di_xorg_ext *ext;
	HASH_FIND(hh, xc->xext, name.data, name.length, ext);
	if (ext) {
		auto ret = tmalloc(union di_value, 1);
		di_ref_object((void *)ext);
		ret->object = (struct di_object *)ext;
		return (struct di_variant){.type = DI_TYPE_OBJECT, .value = ret};
	}
	for (int i = 0; xext_reg[i].name; i++) {
		if (strlen(xext_reg[i].name) != name.length) {
			continue;
		}
		if (memcmp(xext_reg[i].name, name.data, name.length) == 0) {
			auto ext = xext_reg[i].new(xc);
			if (ext == NULL) {
				break;
			}

			di_set_object_dtor((void *)ext, (void *)di_xorg_free_sub);

			HASH_ADD_KEYPTR(hh, xc->xext, ext->extname, strlen(ext->extname), ext);
			di_ref_object((void *)xc);

			auto ret = tmalloc(union di_value, 1);
			ret->object = (struct di_object *)ext;
			return (struct di_variant){.type = DI_TYPE_OBJECT, .value = ret};
		}
	}
	return (struct di_variant){.type = DI_LAST_TYPE, .value = NULL};
}

/// TYPE: deai.plugin.xorg:Screen
struct xscreen {
	struct di_object;
	/// Width of the screen
	///
	/// EXPORT: deai.plugin.xorg:Screen.width, :integer
	uint64_t width;
	/// Height of the screen
	///
	/// EXPORT: deai.plugin.xorg:Screen.height, :integer
	uint64_t height;
};
/// Information about the current screen
///
/// EXPORT: deai.plugin.xorg:Connection.screen, deai.plugin.xorg:Screen
static struct di_object *get_screen(struct di_xorg_connection *dc) {
	auto scrn = screen_of_display(dc->c, dc->dflt_scrn);

	auto ret = di_new_object_with_type2(struct xscreen, "deai.plugin.xorg:Screen");
	ret->height = scrn->height_in_pixels;
	ret->width = scrn->width_in_pixels;

	di_field(ret, height);
	di_field(ret, width);

	return (void *)ret;
}

// A really hacky way of finding all modifiers in a keymap. Because xkbcommon doesn't
// expose an API for that.
static struct {
	int keycode_per_modifiers;
	xcb_keycode_t *keycodes;
} find_modifiers(struct xkb_keymap *map, int min, int max) {
	static const char *modifier_names[8] = {
	    XKB_MOD_NAME_SHIFT, XKB_MOD_NAME_CAPS,
	    XKB_MOD_NAME_CTRL,  XKB_MOD_NAME_ALT,
	    XKB_MOD_NAME_NUM,   "Mod3",
	    XKB_MOD_NAME_LOGO,  "Mod5",
	};

	// keycodes, next_keycode_indices, and modifier_keycode_head effectively
	// forms 8 linked lists. next_keycode_indices are the "next pointers".
	// modifier_keycode_head are the heads of the lists. keycodes are the data
	// stored in the list nodes.
	const int MAX_KEYCODE = 256;
	int modifier_keycode_count[8] = {0};
	int next_keycode_indices[MAX_KEYCODE];
	int keycodes[MAX_KEYCODE];
	int total_keycodes = 0;
	int modifier_keycode_head[8] = {
	    [0 ... 7] = -1,
	};
	int total_modifier_keys = 0;
	memset(next_keycode_indices, -1, sizeof next_keycode_indices);

	// Sanity check
	for (int i = 0; i < 8; i++) {
		assert(xkb_map_mod_get_index(map, modifier_names[i]) != XKB_MOD_INVALID);
	}

	auto state = xkb_state_new(map);
	// Find a non-modifier key
	for (int i = min; i <= max; i++) {
		// Basically we press the key and see what modifier state changes
		auto updates = xkb_state_update_key(state, i, XKB_KEY_DOWN);
		updates &= (XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED |
		            XKB_STATE_MODS_LOCKED);
		if (!updates) {
			xkb_state_update_key(state, i, XKB_KEY_UP);
			continue;
		}
		// printf("%#x %#x\n", i, updates);
		for (int j = 0; j < 8; j++) {
			if (xkb_state_mod_name_is_active(state, modifier_names[j], updates)) {
				// printf("%s %#x\n", modifier_names[j], i);
				next_keycode_indices[total_keycodes] =
				    modifier_keycode_head[j];
				modifier_keycode_head[j] = total_keycodes;
				keycodes[total_keycodes++] = i;
				modifier_keycode_count[j]++;
				total_modifier_keys++;
			}
		}
		xkb_state_update_key(state, i, XKB_KEY_UP);
		if (updates & XKB_STATE_MODS_LOCKED) {
			xkb_state_update_key(state, i, XKB_KEY_DOWN);
			xkb_state_update_key(state, i, XKB_KEY_UP);
		}
		if (updates & XKB_STATE_MODS_LATCHED) {
			// Don't know how to reliably handle mod latches, just
			// recreate the state
			xkb_state_unref(state);
			state = xkb_state_new(map);
		}
	}
	xkb_state_unref(state);

	int keycodes_per_modifiers = 0;
	for (int i = 0; i < 8; i++) {
		if (modifier_keycode_count[i] > keycodes_per_modifiers) {
			keycodes_per_modifiers = modifier_keycode_count[i];
		}
	}

	typeof(find_modifiers(map, min, max)) ret = {0};
	ret.keycode_per_modifiers = keycodes_per_modifiers;
	ret.keycodes = tmalloc(xcb_keycode_t, 8 * keycodes_per_modifiers);

	for (int i = 0; i < 8; i++) {
		int cnt = 0;
		int curr = modifier_keycode_head[i];
		while (curr != -1) {
			assert(cnt < keycodes_per_modifiers);
			ret.keycodes[i * keycodes_per_modifiers + cnt] = keycodes[curr];
			curr = next_keycode_indices[curr];
			cnt++;
		}
	}

	return ret;
}

/// Keyboard mapping
///
/// EXPORT: deai.plugin.xorg:Connection.keymap, :object
///
/// This is a write-only property which allows you to change your keyboard mapping. To set
/// your keymap, you need to provide an object with these members:
///
/// - layout (mandatory): The layout, e.g. 'us', 'gb', etc.
/// - model (optional)
/// - variant (optional)
/// - options (optional)
static void set_keymap(struct di_xorg_connection *xc, struct di_object *o) {
	di_string_with_cleanup layout = DI_STRING_INIT, model = DI_STRING_INIT,
	                       variant = DI_STRING_INIT, options = DI_STRING_INIT;

	di_mgetmi(xc->x, log);
	if (!o || di_get(o, "layout", layout)) {
		di_log_va(logm, DI_LOG_ERROR, "Invalid keymap object, key \"layout\" is not set");
		return;
	}

	struct xkb_rule_names names = {
	    .layout = di_string_to_chars_alloc(layout),
	    .model = NULL,
	    .variant = NULL,
	    .options = NULL,
	};
	if (di_get(o, "model", model) == 0) {
		names.model = di_string_to_chars_alloc(model);
	}
	if (di_get(o, "variant", variant) == 0) {
		names.variant = di_string_to_chars_alloc(variant);
	}
	if (di_get(o, "options", options) == 0) {
		names.options = di_string_to_chars_alloc(options);
	}

	auto xsetup = xcb_get_setup(xc->c);

	auto map = xkb_keymap_new_from_names(xc->xkb_ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	xcb_keysym_t *keysyms = NULL;

	if (xkb_keymap_num_layouts(map) != 1) {
		di_log_va(logm, DI_LOG_ERROR,
		          "Using multiple layout at the same time is not currently "
		          "supported.");
		goto out;
	}

	int keysym_per_keycode = 0;
	int max_keycode = xkb_keymap_max_keycode(map),
	    min_keycode = xkb_keymap_min_keycode(map);
	if (max_keycode > xsetup->max_keycode) {
		// Xorg doesn't accept keycode > 255
		max_keycode = xsetup->max_keycode;
	}
	if (min_keycode < xsetup->min_keycode) {
		min_keycode = xsetup->min_keycode;
	}
	for (int i = min_keycode; i <= max_keycode; i++) {
		int nlevels = xkb_keymap_num_levels_for_key(map, i, 0);
		if (nlevels > keysym_per_keycode) {
			keysym_per_keycode = nlevels;
		}
	}

	// Xorg uses 2 groups of keymapping, while xkbcommon uses 1 group
	keysym_per_keycode += 2;
	keysyms = tmalloc(xcb_keysym_t, keysym_per_keycode * (max_keycode - min_keycode + 1));

	for (int i = min_keycode; i <= max_keycode; i++) {
		int nlevels = xkb_keymap_num_levels_for_key(map, i, 0);
		for (int j = 0; j < nlevels; j++) {
			const xkb_keysym_t *sym;
			int nsyms = xkb_keymap_key_get_syms_by_level(map, i, 0, j, &sym);
			if (nsyms > 1) {
				di_log_va(logm, DI_LOG_WARN,
				          "Multiple keysyms per level is not "
				          "supported");
				continue;
			}
			if (!nsyms || sym == NULL) {
				continue;
			}
			if (j < 2) {
				keysyms[(i - min_keycode) * keysym_per_keycode + j] = *sym;
			}
			keysyms[(i - min_keycode) * keysym_per_keycode + j + 2] = *sym;
		}
	}

	auto r = xcb_request_check(xc->c, xcb_change_keyboard_mapping_checked(
	                                      xc->c, (max_keycode - min_keycode + 1),
	                                      min_keycode, keysym_per_keycode, keysyms));
	if (r) {
		di_log_va(logm, DI_LOG_ERROR, "Failed to set keymap.");
		free(r);
	}

	auto modifiers = find_modifiers(map, min_keycode, max_keycode);

	while (true) {
		auto r2 = xcb_set_modifier_mapping_reply(
		    xc->c,
		    xcb_set_modifier_mapping(xc->c, modifiers.keycode_per_modifiers,
		                             modifiers.keycodes),
		    NULL);
		if (!r2 || r2->status == XCB_MAPPING_STATUS_FAILURE) {
			di_log_va(logm, DI_LOG_ERROR,
			          "Failed to set modifiers, your keymap will be broken.");
			free(r2);
			break;
		}
		if (r2->status == XCB_MAPPING_STATUS_SUCCESS) {
			free(r2);
			break;
		}
	}
	free(modifiers.keycodes);

out:
	free((char *)names.layout);
	free((char *)names.model);
	free((char *)names.variant);
	free((char *)names.options);
	free(keysyms);
	xkb_keymap_unref(map);
}

/// Connect to a X server
///
/// EXPORT: xorg.connect_to(display), deai.plugin.xorg:Connection
///
/// Connect to a X server using an explicit display string.
///
/// Arguments:
///
/// - display(:string) the display
static struct di_object *di_xorg_connect_to(struct di_xorg *x, struct di_string displayname_) {
	int scrn;
	with_cleanup_t(char) displayname = NULL;
	if (displayname_.length > 0) {
		displayname = di_string_to_chars_alloc(displayname_);
	}
	auto c = xcb_connect(displayname, &scrn);
	if (xcb_connection_has_error(c)) {
		xcb_disconnect(c);
		return di_new_error("Cannot connect to the display");
	}

	di_mgetm(x, event, di_new_error("Can't get event module"));

	struct di_xorg_connection *dc =
	    di_new_object_with_type2(struct di_xorg_connection, "deai.plugin.xorg:"
	                                                        "Connection");
	dc->c = c;
	dc->dflt_scrn = scrn;

	struct di_object *xcb_fd_event = NULL;
	di_callr(eventm, "fdevent", xcb_fd_event, xcb_get_file_descriptor(dc->c), IOEV_READ);

	di_weak_object_with_cleanup odc = di_weakly_ref_object((struct di_object *)dc);
	di_closure_with_cleanup cl = di_closure(di_xorg_ioev, (odc));
	auto lh = di_listen_to(xcb_fd_event, di_string_borrow("read"), (void *)cl);

	di_member(dc, "__xcb_fd_event", xcb_fd_event);
	di_member(dc, "__xcb_fd_event_read_listen_handle", lh);

	di_set_object_dtor((void *)dc, (void *)xorg_disconnect);

	di_method(dc, "__get", di_xorg_get_ext, struct di_string);
	di_method(dc, "__get_xrdb", di_xorg_get_resource);
	di_method(dc, "__set_xrdb", di_xorg_set_resource, struct di_string);
	di_method(dc, "__get_screen", get_screen);
	di_method(dc, "__set_keymap", set_keymap, struct di_object *);
	di_method(dc, "disconnect", di_finalize_object);

	dc->x = x;
	dc->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

	return (void *)dc;
}

/// Connect to a X server
///
/// EXPORT: xorg.connect(), deai.plugin.xorg:Connection
///
/// Connect to the default X server, usually the one specified in the DISPLAY environment
/// variable.
static struct di_object *di_xorg_connect(struct di_xorg *x) {
	return di_xorg_connect_to(x, DI_STRING_INIT);
}

/// Xorg
///
/// EXPORT: xorg, deai:module
static struct di_module *new_xorg_module(struct deai *di) {
	auto x = di_new_module(di);

	di_method(x, "connect", di_xorg_connect);
	di_method(x, "connect_to", di_xorg_connect_to, struct di_string);
	return x;
}

DEAI_PLUGIN_ENTRY_POINT(di) {
	auto x = new_xorg_module(di);
	di_register_module(di, di_string_borrow("xorg"), &x);
	return 0;
}
