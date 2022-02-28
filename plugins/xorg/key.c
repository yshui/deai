/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai/builtins/log.h>
#include <deai/helper.h>
#include <deai/object.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon.h>
#include "list.h"
#include "string_buf.h"
#include "xorg.h"

struct xorg_key {
	struct di_xorg_ext;

	xcb_key_symbols_t *keysyms;

	struct list_head bindings;
	int nsignals;
};

struct keybinding {
	struct di_object;

	xcb_keysym_t keysym;        // really needed?
	xcb_keycode_t *keycodes;
	uint16_t modifiers;
	struct xorg_key *k;
	struct list_head siblings;
	bool replay;
};

static const char *const modifier_names[] = {
    "shift", "lock", "control", "mod1", "mod2", "mod3", "mod4", "mod5",
};

static int name_to_mod(struct di_string keyname) {
	for (int i = 0, mask = 1; i < ARRAY_SIZE(modifier_names); i++, mask *= 2) {
		if (strncasecmp(keyname.data, modifier_names[i], keyname.length) == 0) {
			return mask;
		}
	}
	if (strncasecmp(keyname.data, "ctrl", keyname.length) == 0) {
		// Alternative name for control
		return XCB_MOD_MASK_CONTROL;
	}
	if (strncasecmp(keyname.data, "any", keyname.length) == 0) {
		/* this is misnamed but correct */
		return XCB_BUTTON_MASK_ANY;
	}
	return XCB_NO_SYMBOL;
}

static void ungrab(struct keybinding *kb) {
	di_object_with_cleanup dc_obj = NULL;
	if (di_get(kb->k, XORG_CONNECTION_MEMBER, dc_obj) != 0) {
		return;
	}
	struct di_xorg_connection *dc = (void *)dc_obj;
	if (dc != NULL) {
		auto s = screen_of_display(dc->c, dc->dflt_scrn);
		for (int i = 0; kb->keycodes[i] != XCB_NO_SYMBOL; i++) {
			xcb_ungrab_key(dc->c, kb->keycodes[i], s->root, kb->modifiers);
		}
	}
}

static char *describe_keybinding(struct keybinding *kb) {
	const size_t MAX_KEYNAME_LEN = 128;
	char keyname[MAX_KEYNAME_LEN];
	if (xkb_keysym_get_name(kb->keysym, keyname, sizeof(keyname)) == -1) {
		strcpy(keyname, "(invalid)");
	}

	auto buf = string_buf_new();
	bool first_modifier = true;
	for (int i = 0, mask = 1; i < ARRAY_SIZE(modifier_names); i++, mask *= 2) {
		if (kb->modifiers & mask) {
			if (!first_modifier) {
				string_buf_push(buf, "+");
			}
			string_buf_push(buf, modifier_names[i]);
			first_modifier = false;
		}
	}
	if (!first_modifier) {
		string_buf_push(buf, "+");
	}
	string_buf_push(buf, keyname);

	char *ret = string_buf_dump(buf);
	free(buf);

	return ret;
}

static void binding_dtor(struct keybinding *kb) {
	ungrab(kb);
	free(kb->keycodes);
	list_del(&kb->siblings);
	di_unref_object((void *)kb->k);
	kb->k = NULL;
}

static int refresh_binding(struct keybinding *kb) {
	di_object_with_cleanup dc_obj = NULL;
	if (di_get(kb->k, XORG_CONNECTION_MEMBER, dc_obj) != 0) {
		return -1;
	}
	struct di_xorg_connection *dc = (void *)dc_obj;
	if (kb->keycodes) {
		ungrab(kb);
		free(kb->keycodes);
		kb->keycodes = NULL;
	}

	auto kc = xcb_key_symbols_get_keycode(kb->k->keysyms, kb->keysym);
	if (kc == NULL) {
		return -1;
	}

	kb->keycodes = kc;

	auto s = screen_of_display(dc->c, dc->dflt_scrn);
	for (int i = 0; kb->keycodes[i] != XCB_NO_SYMBOL; i++) {
		auto err = xcb_request_check(
		    dc->c, xcb_grab_key_checked(dc->c, true, s->root, kb->modifiers,
		                                kb->keycodes[i], XCB_GRAB_MODE_ASYNC,
		                                XCB_GRAB_MODE_SYNC));
		if (err) {
			di_mgetmi(dc->x, log);
			if (logm) {
				char *description = describe_keybinding(kb);
				di_log_va(logm, DI_LOG_ERROR,
				          "Cannot grab %#x, for keybinding %s\n",
				          kb->keycodes[i], description);
				free(description);
			}
			free(err);
		}
	}
	return 0;
}
static bool key_register_listener(struct xorg_key *k);
static void key_deregister_listener(struct xorg_key *k);
static void
keybinding_new_signal(const char *signal, struct di_object *obj, struct di_object *sig) {
	bool had_signal = di_has_member(obj, "__signal_pressed") ||
	                  di_has_member(obj, "__signal_released");
	if (di_add_member_clone(obj, di_string_borrow(signal), DI_TYPE_OBJECT, &sig) != 0) {
		return;
	}
	if (had_signal) {
		return;
	}

	bool success = true;
	struct keybinding *kb = (void *)obj;
	di_string_with_cleanup keybinding_key = DI_STRING_INIT;
	if (refresh_binding(kb) != 0) {
		goto early_err;
	}
	kb->k->nsignals += 1;
	if (kb->k->nsignals == 1) {
		success = key_register_listener(kb->k);
	}

	// Keep ourself alive
	keybinding_key = di_string_printf("___keybinding_%p", obj);
	success = di_add_member_clone((void *)kb->k, keybinding_key, DI_TYPE_OBJECT, &obj) == 0;
	if (success) {
		return;
	}
	ungrab(kb);
early_err:
	di_remove_member(obj, di_string_borrow(signal));
}
static void keybinding_del_signal(const char *signal, struct di_object *obj) {
	if (di_remove_member_raw(obj, di_string_borrow(signal)) != 0) {
		return;
	}
	bool has_signal = di_has_member(obj, "__signal_pressed") ||
	                  di_has_member(obj, "__signal_released");
	if (has_signal) {
		return;
	}
	struct keybinding *kb = (void *)obj;
	ungrab(kb);
	kb->k->nsignals -= 1;
	if (kb->k->nsignals == 0) {
		key_deregister_listener(kb->k);
	}

	// Stop keeping ourself alive
	di_string_with_cleanup keybinding_key = di_string_printf("___keybinding_%p", obj);
	di_remove_member_raw((void *)kb->k, keybinding_key);
}
/// Add a new key binding
///
/// EXPORT: deai.plugin.xorg:Key.new(modifiers, key, replay), deai.plugin.xorg.key:Binding
///
/// Create a new event source that emits a signal when a given key binding is pressed or
/// released.
///
/// Arguments:
///
/// - modifiers([:string]) the modifier keys, valid ones are: mod1~5, shift, control, alt.
/// - replay(:bool) whether the key press event will be passed on. If false, deai will
///                 intercept the key press, otherwise it will behave like a normal key
///                 press.
/// - key(:string)
struct di_object *
new_binding(struct xorg_key *k, struct di_array modifiers, struct di_string key, bool replay) {
	di_object_with_cleanup dc_obj = NULL;
	if (di_get(k, XORG_CONNECTION_MEMBER, dc_obj) != 0) {
		return di_new_error("Connection died");
	}

	with_cleanup_t(char) key_str = di_string_to_chars_alloc(key);
	xkb_keysym_t ks = xkb_keysym_from_name(key_str, XKB_KEYSYM_CASE_INSENSITIVE);
	if (ks == XKB_KEY_NoSymbol) {
		return di_new_error("Invalid key name");
	}

	if (modifiers.length > 0 && modifiers.elem_type != DI_TYPE_STRING) {
		return di_new_error("Invalid modifiers");
	}

	int mod = 0;
	struct di_string *arr = modifiers.arr;
	for (int i = 0; i < modifiers.length; i++) {
		int tmp = name_to_mod(arr[i]);
		if (tmp == XCB_NO_SYMBOL) {
			return di_new_error("Invalid modifiers");
		}
		mod |= tmp;
	}

	auto kb = di_new_object_with_type(struct keybinding);
	kb->modifiers = mod;
	kb->keysym = ks;
	di_set_object_dtor((void *)kb, (void *)binding_dtor);
	kb->k = k;
	kb->replay = replay;
	di_ref_object((void *)k);
	list_add(&kb->siblings, &k->bindings);

	di_signal_setter_deleter_with_signal_name(kb, "pressed", keybinding_new_signal,
	                                          keybinding_del_signal);
	di_signal_setter_deleter_with_signal_name(kb, "released", keybinding_new_signal,
	                                          keybinding_del_signal);

	int ret = refresh_binding(kb);
	if (ret != 0) {
		di_unref_object((void *)kb);
		return di_new_error("Failed to setup key grab");
	}

	return (void *)kb;
}

static void free_key(struct xorg_key *k) {
	xcb_key_symbols_free(k->keysyms);
	struct keybinding *kb, *nkb;
	list_for_each_entry_safe (kb, nkb, &k->bindings, siblings) {
		di_finalize_object((void *)kb);
	}
}
define_trivial_cleanup_t(xcb_get_modifier_mapping_reply_t);
uint16_t mod_from_keycode(struct di_xorg_connection *dc, xcb_keycode_t kc) {
	with_cleanup_t(xcb_get_modifier_mapping_reply_t) r =
	    xcb_get_modifier_mapping_reply(dc->c, xcb_get_modifier_mapping(dc->c), NULL);
	if (r == NULL || !r->keycodes_per_modifier) {
		return 0;
	}
	auto kcs = xcb_get_modifier_mapping_keycodes(r);
	if (kcs == NULL) {
		return 0;
	}
	uint16_t ret = 0;
	for (uint32_t i = 0; i < r->length; i++) {
		if (kcs[i] != XCB_NO_SYMBOL && kcs[i] == kc) {
			ret |= (1 << (i / r->keycodes_per_modifier));
		}
	}
	return ret;
}

/// SIGNAL: deai.plugin.xorg.key:Binding.pressed() key binding is pressed
///
/// SIGNAL: deai.plugin.xorg.key:Binding.released() key binding is released
static void handle_key(struct di_xorg_ext *ext, xcb_generic_event_t *ev) {
	di_object_with_cleanup dc_obj = NULL;
	if (di_get(ext, XORG_CONNECTION_MEMBER, dc_obj) != 0) {
		return;
	}
	struct di_xorg_connection *dc = (void *)dc_obj;
	xcb_keycode_t kc;
	uint16_t mod;
	const char *event;
	struct xorg_key *k = (void *)ext;
	switch (ev->response_type) {
	case XCB_KEY_PRESS:;
		xcb_key_press_event_t *pe = (void *)ev;
		kc = pe->detail;
		mod = pe->state;
		event = "pressed";
		break;
	case XCB_KEY_RELEASE:;
		xcb_key_release_event_t *re = (void *)ev;
		kc = re->detail;
		// Mod key release events will have that modifier set in the
		// state, which is counter-intuitive. And require user to
		// create two bindings in order to handle mod key press and
		// release, instead of just one.
		// So we strip out the modifier that is being released
		mod = re->state & ~mod_from_keycode(dc, kc);
		event = "released";
		break;
	case XCB_MAPPING_NOTIFY:;
		xcb_mapping_notify_event_t *me = (void *)ev;
		if (me->request == XCB_MAPPING_POINTER) {
			return;
		}
		if (xcb_refresh_keyboard_mapping(k->keysyms, me) == 1) {
			struct keybinding *kb;
			list_for_each_entry (kb, &k->bindings, siblings) {
				int ret = refresh_binding(kb);
				if (ret != 0) {
					di_finalize_object((void *)kb);
				}
			}
		}
		return;
	default:
		return;
	}

	struct keybinding *kb, *nkb;
	list_for_each_entry (kb, &k->bindings, siblings) { di_ref_object((void *)kb); }

	bool replay = true;
	list_for_each_entry_safe (kb, nkb, &k->bindings, siblings) {
		__label__ match, end;
		if (kb->modifiers != mod) {
			continue;
		}
		for (int i = 0; kb->keycodes[i] != XCB_NO_SYMBOL; i++) {
			if (kb->keycodes[i] == kc) {
				goto match;
			}
		}
	end:
		di_unref_object((void *)kb);
		continue;
	match:
		replay = kb->replay;
		di_emit(kb, event);
		goto end;
	}

	if (replay) {
		xcb_allow_events(dc->c, XCB_ALLOW_REPLAY_KEYBOARD, XCB_CURRENT_TIME);
	} else {
		xcb_allow_events(dc->c, XCB_ALLOW_SYNC_KEYBOARD, XCB_CURRENT_TIME);
	}
	xcb_flush(dc->c);
}

const int OPCODES[] = {XCB_KEY_PRESS, XCB_KEY_RELEASE, XCB_MAPPING_NOTIFY};
bool key_register_listener(struct xorg_key *k) {
	di_object_with_cleanup handler =
	    (void *)di_closure(handle_key, ((struct di_object *)k), void *);
	di_object_with_cleanup dc = NULL;
	if (di_get(k, XORG_CONNECTION_MEMBER, dc) != 0) {
		return false;
	}

	fprintf(stderr, "reg\n");
	for (int i = 0; i < ARRAY_SIZE(OPCODES); i++) {
		di_string_with_cleanup signal =
		    di_string_printf("___raw_x_event_%d", OPCODES[i]);
		di_object_with_cleanup lh = di_listen_to(dc, signal, handler);
		struct di_object *autolh = NULL;
		di_string_with_cleanup autolh_key =
		    di_string_printf("___auto_handle_for_%d", OPCODES[i]);
		DI_CHECK_OK(di_callr(lh, "auto_stop", autolh));
		DI_CHECK_OK(di_add_member_move((void *)k, autolh_key,
		                               (di_type_t[]){DI_TYPE_OBJECT}, &autolh));
	}
	return true;
}
void key_deregister_listener(struct xorg_key *k) {
	fprintf(stderr, "dereg\n");
	for (int i = 0; i < ARRAY_SIZE(OPCODES); i++) {
		di_string_with_cleanup autolh_key =
		    di_string_printf("___auto_handle_for_%d", OPCODES[i]);
		di_remove_member_raw((void *)k, autolh_key);
	}
}

/// Key bindings
///
/// EXPORT: deai.plugin.xorg:Connection.key, deai.plugin.xorg:Key
///
/// Manage keyboard short cuts.
struct di_xorg_ext *new_key(struct di_xorg_connection *dc) {
	auto k = di_new_object_with_type2(struct xorg_key, "deai.plugin.xorg:Key");
	k->extname = "key";

	k->keysyms = xcb_key_symbols_alloc(dc->c);

	INIT_LIST_HEAD(&k->bindings);
	di_method(k, "new", new_binding, struct di_array, struct di_string, bool);
	di_set_object_dtor((void *)k, (void *)free_key);
	return (void *)k;
}
