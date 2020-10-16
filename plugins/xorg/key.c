/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai/builtins/log.h>
#include <deai/helper.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon.h>
#include "list.h"
#include "string_buf.h"
#include "xorg.h"

define_trivial_cleanup_t(char);
struct xorg_key {
	struct di_xorg_ext;

	xcb_key_symbols_t *keysyms;

	struct list_head bindings;
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
	if (kb->k->dc != NULL) {
		auto s = screen_of_display(kb->k->dc->c, kb->k->dc->dflt_scrn);
		for (int i = 0; kb->keycodes[i] != XCB_NO_SYMBOL; i++) {
			xcb_ungrab_key(kb->k->dc->c, kb->keycodes[i], s->root, kb->modifiers);
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

	auto dc = kb->k->dc;
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

struct di_object *
new_binding(struct xorg_key *k, struct di_array modifiers, struct di_string key, bool replay) {
	if (!k->dc) {
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

	int ret = refresh_binding(kb);
	if (ret != 0) {
		di_unref_object((void *)kb);
		return di_new_error("Failed to setup key grab");
	}

	di_method(kb, "stop", di_finalize_object);
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

static int handle_key(struct di_xorg_ext *ext, xcb_generic_event_t *ev) {
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
		mod = re->state & ~mod_from_keycode(ext->dc, kc);
		event = "released";
		break;
	case XCB_MAPPING_NOTIFY:;
		xcb_mapping_notify_event_t *me = (void *)ev;
		if (me->request == XCB_MAPPING_POINTER) {
			return 1;
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
		return 1;
	default:
		return 1;
	}

	struct keybinding *kb, *nkb;
	list_for_each_entry (kb, &k->bindings, siblings)
		di_ref_object((void *)kb);

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
		xcb_allow_events(ext->dc->c, XCB_ALLOW_REPLAY_KEYBOARD, XCB_CURRENT_TIME);
	} else {
		xcb_allow_events(ext->dc->c, XCB_ALLOW_SYNC_KEYBOARD, XCB_CURRENT_TIME);
	}
	xcb_flush(ext->dc->c);
	return 0;
}

struct di_xorg_ext *new_key(struct di_xorg_connection *dc) {
	auto k = di_new_object_with_type2(struct xorg_key, "deai.plugin.xorg:Key");
	k->dc = dc;
	k->free = (void *)free_key;
	k->handle_event = handle_key;
	k->extname = "key";

	k->keysyms = xcb_key_symbols_alloc(dc->c);

	INIT_LIST_HEAD(&k->bindings);

	di_method(k, "new", new_binding, struct di_array, struct di_string, bool);
	return (void *)k;
}
