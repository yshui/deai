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

#include "xorg.h"

struct di_atom_entry {
	di_string name;
	xcb_atom_t atom;

	UT_hash_handle hh, hh2;
};

define_trivial_cleanup(xcb_generic_error_t);

/// Disconnect from the X server
///
/// EXPORT: deai.plugin.xorg:Connection.disconnect(): :void
///
/// Disconnecting from the X server will stop all related event sources. All objects
/// coming from this connection will stop generating any events after this.
/// You should stop using the Connection object after you have called disconnect.
static void xorg_disconnect(di_xorg_connection *xc) {
	if (xc->xkb_ctx) {
		xkb_context_unref(xc->xkb_ctx);
	}

	DI_CHECK(xc->c != NULL);
	xcb_disconnect(xc->c);
	xc->c = NULL;
	xc->nsignals = 0;

	// Drop the auto stop handle to stop the signal listener
	di_delete_member_raw((void *)xc,
	                     di_string_borrow_literal("__xcb_fd_event_read_listen_handle"));

	struct di_atom_entry *ae, *tae;
	HASH_ITER (hh, xc->a_byatom, ae, tae) {
		HASH_DEL(xc->a_byatom, ae);
		HASH_DELETE(hh2, xc->a_byname, ae);
		di_free_string(ae->name);
		free(ae);
	}
}

struct _xext {
	const char *name;
	struct di_xorg_ext *(*new)(di_xorg_connection *xc);
} xext_reg[] = {
    {"xinput", new_xinput},
    {"randr", new_randr},
    {"key", new_key},
    {NULL, NULL},
};

static void di_xorg_ioev(di_object *dc_obj) {
	// di_get_log(dc->x->di);
	// di_log_va((void *)log, DI_LOG_DEBUG, "xcb ioev\n");
	auto dc = (di_xorg_connection *)dc_obj;
	xcb_generic_event_t *ev;

	while ((ev = xcb_poll_for_event(dc->c))) {
		// handle event
		{
			scoped_di_string event_name = DI_STRING_INIT;
			if (ev->response_type == XCB_GE_GENERIC) {
				auto gev = (xcb_ge_generic_event_t *)ev;
				event_name = di_string_printf("___raw_x_event_ge_%d", gev->event_type);
			} else {
				event_name = di_string_printf("___raw_x_event_%d", ev->response_type);
			}
			di_value tmp;
			tmp.pointer = ev;
			di_emitn((void *)dc, event_name,
			         (di_tuple){
			             .length = 1,
			             .elements =
			                 &(struct di_variant){
			                     .type = DI_TYPE_POINTER,
			                     .value = &tmp,
			                 },
			         });
		}

		for (int i = 0; xext_reg[i].name != NULL; i++) {
			// Only strongly referenced ext have signal listerners.
			scoped_di_string ext_key = di_string_printf("___strong_x_ext_%s", xext_reg[i].name);
			scoped_di_object *ext_obj = NULL;
			if (di_get2(dc_obj, ext_key, ext_obj) != 0) {
				continue;
			}
			struct di_xorg_ext *ext = (void *)ext_obj;
			if (ext->handle_event == NULL) {
				continue;
			}
			int status = ext->handle_event(ext, ev);
			if (status != 1) {
				break;
			}
		}
		free(ev);
	}

	if (xcb_connection_has_error(dc->c)) {
		di_emit(dc, "connection-error");
		di_finalize_object((di_object *)dc);
	}
}

const di_string *di_xorg_get_atom_name(di_xorg_connection *xc, xcb_atom_t atom) {
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

xcb_atom_t
di_xorg_intern_atom(di_xorg_connection *xc, di_string name, xcb_generic_error_t **e) {
	struct di_atom_entry *ae = NULL;
	*e = NULL;

	HASH_FIND(hh2, xc->a_byname, name.data, name.length, ae);
	if (ae) {
		return ae->atom;
	}

	auto r = xcb_intern_atom_reply(xc->c, xcb_intern_atom(xc->c, 0, name.length, name.data), e);
	if (!r) {
		di_log_va(log_module, DI_LOG_ERROR, "Cannot intern atom");
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
/// EXPORT: deai.plugin.xorg:Connection.xrdb: :string
///
/// This property corresponds to the xrdb, which is usually set with the command
/// line tool with the same name. Assigning to this property updates the xrdb.
static di_string di_xorg_get_resource(di_xorg_connection *xc) {
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

static void di_xorg_set_resource(di_xorg_connection *xc, di_string rdb) {
	auto scrn = screen_of_display(xc->c, xc->dflt_scrn);
	scopedp(xcb_generic_error_t) *e = xcb_request_check(
	    xc->c, xcb_change_property(xc->c, XCB_PROP_MODE_REPLACE, scrn->root, XCB_ATOM_RESOURCE_MANAGER,
	                               XCB_ATOM_STRING, 8, rdb.length, rdb.data));
	(void)e;
}

static struct di_variant di_xorg_get_ext(di_xorg_connection *xc, di_string name) {
	scoped_di_weak_object *weak_ext = NULL;
	scoped_di_string ext_member =
	    di_string_concat(di_string_borrow_literal("___weak_x_ext_"), name);
	di_value tmp;
	if (di_rawgetxt((void *)xc, ext_member, DI_TYPE_WEAK_OBJECT, &tmp) == 0) {
		weak_ext = tmp.weak_object;
	}
	if (weak_ext) {
		auto ext = di_upgrade_weak_ref(weak_ext);
		if (ext != NULL) {
			return di_variant_of(ext);
		}
		di_delete_member_raw((void *)xc, ext_member);
	}
	for (int i = 0; xext_reg[i].name; i++) {
		if (!di_string_eq(di_string_borrow(xext_reg[i].name), name)) {
			continue;
		}
		auto ext = xext_reg[i].new(xc);
		if (ext == NULL) {
			break;
		}
		weak_ext = di_weakly_ref_object((void *)ext);
		DI_CHECK_OK(di_add_member_clone((void *)xc, ext_member, DI_TYPE_WEAK_OBJECT, &weak_ext));
		return di_variant_of((di_object *)ext);
	}
	return (struct di_variant){.type = DI_LAST_TYPE, .value = NULL};
}

/// TYPE: deai.plugin.xorg:Screen
struct xscreen {
	di_object;
	/// Width of the screen
	///
	/// EXPORT: deai.plugin.xorg:Screen.width: :integer
	uint64_t width;
	/// Height of the screen
	///
	/// EXPORT: deai.plugin.xorg:Screen.height: :integer
	uint64_t height;
};
/// Information about the current screen
///
/// EXPORT: deai.plugin.xorg:Connection.screen: deai.plugin.xorg:Screen
static di_object *get_screen(struct di_xorg_connection *dc) {
	auto scrn = screen_of_display(dc->c, dc->dflt_scrn);

	auto ret = di_new_object_with_type2(struct xscreen, "deai.plugin.xorg:Screen");
	ret->height = scrn->height_in_pixels;
	ret->width = scrn->width_in_pixels;

	di_field(ret, height);
	di_field(ret, width);

	return (void *)ret;
}

// A really hacky way of finding all modifiers in a keymap. Because xkbcommon
// doesn't expose an API for that.
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
		updates &= (XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED | XKB_STATE_MODS_LOCKED);
		if (!updates) {
			xkb_state_update_key(state, i, XKB_KEY_UP);
			continue;
		}
		// printf("%#x %#x\n", i, updates);
		for (int j = 0; j < 8; j++) {
			if (xkb_state_mod_name_is_active(state, modifier_names[j], updates)) {
				// printf("%s %#x\n", modifier_names[j], i);
				next_keycode_indices[total_keycodes] = modifier_keycode_head[j];
				modifier_keycode_head[j] = total_keycodes;
				keycodes[total_keycodes++] = i;
				modifier_keycode_count[j]++;
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
/// EXPORT: deai.plugin.xorg:Connection.keymap: :object
///
/// This is a write-only property which allows you to change your keyboard
/// mapping. To set your keymap, you need to provide an object with these members:
///
/// - layout (mandatory): The layout, e.g. 'us', 'gb', etc.
/// - model (optional)
/// - variant (optional)
/// - options (optional)
static void set_keymap(di_xorg_connection *xc, di_object *o) {
	scoped_di_string layout = DI_STRING_INIT, model = DI_STRING_INIT,
	                 variant = DI_STRING_INIT, options = DI_STRING_INIT;

	if (!o || di_get(o, "layout", layout)) {
		di_log_va(log_module, DI_LOG_ERROR, "Invalid keymap object, key \"layout\" is not set");
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
		di_log_va(log_module, DI_LOG_ERROR,
		          "Using multiple layout at the same time is not currently "
		          "supported.");
		goto out;
	}

	unsigned int keysym_per_keycode = 0;
	auto max_keycode = xkb_keymap_max_keycode(map);
	auto min_keycode = xkb_keymap_min_keycode(map);
	if (max_keycode > xsetup->max_keycode) {
		// Xorg doesn't accept keycode > 255
		max_keycode = xsetup->max_keycode;
	}
	if (min_keycode < xsetup->min_keycode) {
		min_keycode = xsetup->min_keycode;
	}
	for (auto i = min_keycode; i <= max_keycode; i++) {
		auto nlevels = xkb_keymap_num_levels_for_key(map, i, 0);
		if (nlevels > keysym_per_keycode) {
			keysym_per_keycode = nlevels;
		}
	}

	// Xorg uses 2 groups of keymapping, while xkbcommon uses 1 group
	keysym_per_keycode += 2;
	keysyms =
	    tmalloc(xcb_keysym_t, (long)keysym_per_keycode * (max_keycode - min_keycode + 1));

	for (auto i = min_keycode; i <= max_keycode; i++) {
		auto nlevels = xkb_keymap_num_levels_for_key(map, i, 0);
		for (int j = 0; j < nlevels; j++) {
			const xkb_keysym_t *sym;
			int nsyms = xkb_keymap_key_get_syms_by_level(map, i, 0, j, &sym);
			if (nsyms > 1) {
				di_log_va(log_module, DI_LOG_WARN,
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

	auto r = xcb_request_check(
	    xc->c, xcb_change_keyboard_mapping_checked(xc->c, (max_keycode - min_keycode + 1),
	                                               min_keycode, keysym_per_keycode, keysyms));
	if (r) {
		di_log_va(log_module, DI_LOG_ERROR, "Failed to set keymap.");
		free(r);
	}

	auto modifiers = find_modifiers(map, min_keycode, max_keycode);

	while (true) {
		auto r2 = xcb_set_modifier_mapping_reply(
		    xc->c,
		    xcb_set_modifier_mapping(xc->c, modifiers.keycode_per_modifiers, modifiers.keycodes),
		    NULL);
		if (!r2 || r2->status == XCB_MAPPING_STATUS_FAILURE) {
			di_log_va(log_module, DI_LOG_ERROR,
			          "Failed to set modifiers, your keymap will be "
			          "broken.");
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

void print_stack_trace(int, int);
void di_xorg_add_signal(di_xorg_connection *xc) {
	xc->nsignals += 1;
	if (xc->nsignals != 1) {
		return;
	}
	scoped_di_weak_object *weak_event = NULL;
	DI_CHECK_OK(di_get(xc, "__weak_event_module", weak_event));
	scoped_di_object *eventm = di_upgrade_weak_ref(weak_event);
	if (eventm == NULL) {
		// mostly likely deai is shutting down
		log_info("cannot find event module");
		return;
	}

	scoped_di_object *xcb_fd_event = NULL;
	DI_CHECK_OK(di_callr(eventm, "fdevent", xcb_fd_event, xcb_get_file_descriptor(xc->c)));
	scoped_di_closure *cl = di_make_closure(di_xorg_ioev, ((di_object *)xc));
	auto lh = di_listen_to(xcb_fd_event, di_string_borrow("read"), (void *)cl);

	DI_CHECK_OK(di_call(lh, "auto_stop", true));
	di_member(xc, "__xcb_fd_event_read_listen_handle", lh);
}

void di_xorg_del_signal(di_xorg_connection *xc) {
	xc->nsignals -= 1;
	if (xc->nsignals != 0) {
		return;
	}
	// Drop the auto stop handle to stop the listener
	di_delete_member_raw((void *)xc,
	                     di_string_borrow_literal("__xcb_fd_event_read_listen_handle"));
}

void di_xorg_signal_setter(di_object *obj, di_string member, di_object *sig) {
	if (!di_string_starts_with(member, "__signal_")) {
		return;
	}
	if (di_add_member_clone(obj, member, DI_TYPE_OBJECT, &sig) != 0) {
		return;
	}
	di_xorg_add_signal((void *)obj);
}

void di_xorg_signal_deleter(di_object *obj, di_string member) {
	if (!di_string_starts_with(member, "__signal_")) {
		return;
	}
	if (di_delete_member_raw(obj, member) != 0) {
		return;
	}
	di_xorg_del_signal((void *)obj);
}

void di_xorg_ext_signal_setter(const char *signal, di_object *obj, di_object *sig) {
	scoped_di_object *dc_obj = NULL;
	if (di_get(obj, XORG_CONNECTION_MEMBER, dc_obj) != 0) {
		return;
	}
	if (di_member_clone(obj, signal, sig) != 0) {
		return;
	}

	// Keep ext object alive while it has signals
	struct di_xorg_ext *ext = (void *)obj;
	ext->nsignals += 1;
	if (ext->nsignals == 1) {
		scoped_di_string strong_ext_member =
		    di_string_printf("___strong_x_ext_%s", ext->extname);
		if (di_add_member_clone(dc_obj, strong_ext_member, DI_TYPE_OBJECT, &ext) != 0) {
			return;
		}
		di_xorg_add_signal((void *)dc_obj);
	}
}

void di_xorg_ext_signal_deleter(const char *signal, di_object *obj) {
	scoped_di_object *dc_obj = NULL;
	if (di_get(obj, XORG_CONNECTION_MEMBER, dc_obj) != 0) {
		return;
	}
	if (di_delete_member_raw(obj, di_string_borrow(signal)) != 0) {
		return;
	}
	struct di_xorg_ext *ext = (void *)obj;
	ext->nsignals -= 1;
	if (ext->nsignals == 0) {
		scoped_di_string strong_ext_member =
		    di_string_printf("___strong_x_ext_%s", ext->extname);
		di_delete_member_raw(dc_obj, strong_ext_member);
		di_xorg_del_signal((void *)dc_obj);
	}
}

static di_object *di_xorg_new_clipboard(struct di_xorg *x) {
}

static void di_xorg_load_builtin_lua(di_object *x) {
	di_object *di = di_object_borrow_deai(x);
	di_object *lua_module;
	if (di_rawget_borrowed(di, "lua", lua_module) != 0) {
		// lua not enabled or deai is shutting down
		return;
	}

	scoped_di_string resources_dir = DI_STRING_INIT;
	DI_CHECK_OK(di_get(di, "resources_dir", resources_dir));
	scoped_di_string builtin_path = di_string_printf(
	    "%.*s/xorg/builtins.lua", (int)resources_dir.length, resources_dir.data);
	di_tuple ret_values;
	di_object *builtins = NULL;
	if (di_callr(lua_module, "load_script", ret_values, builtin_path) != 0 ||
	    ret_values.length == 0) {
		log_warn("Failed to load builtins.lua for xorg plugin");
	} else if (ret_values.length != 1 || ret_values.elements[0].type != DI_TYPE_OBJECT) {
		di_free_value(DI_TYPE_TUPLE, (di_value *)&ret_values);
		log_error("Unexpected return value from xorg builtins.lua");
	} else {
		builtins = ret_values.elements[0].value->object;
		free(ret_values.elements[0].value);
		free(ret_values.elements);
	}

	if (builtins == NULL) {
		// If we failed to load the builtins, create a dummy object so we won't
		// repeatedly try to load it.
		builtins = di_new_object_with_type(di_object);
	}
	di_member(x, builtin_member_name, builtins);
}

/// Connect to a X server
///
/// EXPORT: xorg.connect_to(display): deai.plugin.xorg:Connection
///
/// Connect to a X server using an explicit display string.
///
/// Arguments:
///
/// - display(:string) the display
static di_object *di_xorg_connect_to(di_object *x, di_string displayname_) {
	di_xorg_load_builtin_lua(x);

	di_object *builtins = NULL;
	DI_CHECK_OK(di_get(x, builtin_member_name, builtins));

	int scrn;
	scopedp(char) *displayname = NULL;
	if (displayname_.length > 0) {
		displayname = di_string_to_chars_alloc(displayname_);
	}
	auto c = xcb_connect(displayname, &scrn);
	if (xcb_connection_has_error(c)) {
		xcb_disconnect(c);
		return di_new_error("Cannot connect to the display");
	}

	di_mgetm(x, event, di_new_error("Can't get event module"));

	auto dc = di_new_object_with_type2(di_xorg_connection, "deai.plugin.xorg:Connection");
	dc->c = c;
	dc->dflt_scrn = scrn;
	dc->nsignals = 0;

	scoped_di_weak_object *weak_eventm = di_weakly_ref_object(eventm);
	di_member(dc, "__weak_event_module", weak_eventm);

	di_set_object_dtor((void *)dc, (void *)xorg_disconnect);

	di_method(dc, "__get", di_xorg_get_ext, di_string);
	di_method(dc, "__set", di_xorg_signal_setter, di_string, di_object *);
	di_method(dc, "__delete", di_xorg_signal_deleter, di_string);
	di_method(dc, "__get_xrdb", di_xorg_get_resource);
	di_method(dc, "__set_xrdb", di_xorg_set_resource, di_string);
	di_method(dc, "__get_screen", get_screen);
	di_method(dc, "__set_keymap", set_keymap, di_object *);
	di_method(dc, "__get_clipboard", di_xorg_new_clipboard);
	di_method(dc, "disconnect", di_finalize_object);

	di_member(dc, builtin_member_name, builtins);

	dc->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

	return (void *)dc;
}

void di_xorg_copy_from_builtins(di_object *target, const char *path, di_object *builtins) {
	const char *pos = path;
	scoped_di_object *source = di_ref_object(builtins);
	while (pos && *pos) {
		const char *next = strchr(pos, '.');
		if (next == NULL) {
			next = pos + strlen(pos);
		}
		scoped_di_string member = di_string_ndup(pos, next - pos);
		di_object *value = NULL;
		if (di_get2(builtins, member, value) == 0) {
			di_unref_object(source);
			source = value;
		} else {
			return;
		}
		pos = next;
		if (*pos == '.') {
			pos++;
		}
	}

	scoped_di_string key = DI_STRING_INIT;
	while (true) {
		scoped_di_tuple member = di_object_next_member(source, key);
		if (member.length < 2) {
			break;
		}
		DI_CHECK(member.elements[0].type == DI_TYPE_STRING);
		if (member.elements[1].type == DI_TYPE_OBJECT) {
			di_add_member_move(target, member.elements[0].value->string,
			                   &member.elements[1].type, member.elements[1].value);
		}
		di_string tmp = key;
		key = member.elements[0].value->string;
		member.elements[0].value->string = tmp;
	}
}

/// Connect to a X server
///
/// EXPORT: xorg.connect(): deai.plugin.xorg:Connection
///
/// Connect to the default X server, usually the one specified in the DISPLAY
/// environment variable.
static di_object *di_xorg_connect(di_object *x) {
	return di_xorg_connect_to(x, DI_STRING_INIT);
}

/// Xorg
///
/// EXPORT: xorg: deai:module
static struct di_module *new_xorg_module(struct deai *di) {
	auto x = di_new_module(di);

	di_method(x, "connect", di_xorg_connect);
	di_method(x, "connect_to", di_xorg_connect_to, di_string);
	return x;
}

DEAI_PLUGIN_ENTRY_POINT(di) {
	auto x = new_xorg_module(di);
	di_register_module(di, di_string_borrow("xorg"), &x);
	return 0;
}
