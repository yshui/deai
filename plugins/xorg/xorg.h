/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

#include <deai/deai.h>
#include <deai/helper.h>

#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon.h>

#include "uthash.h"
#include "utils.h"

struct di_xorg;

struct di_atom_entry;
struct di_xorg_ext;

typedef struct di_xorg_connection {
	struct di_object;
	xcb_connection_t *nullable c;
	int dflt_scrn;
	int nsignals;
	struct di_xorg_ext *nullable xext;

	struct xkb_context *nullable xkb_ctx;
	struct di_atom_entry *nullable a_byatom, *nullable a_byname;
} di_xorg_connection;

struct di_xorg_ext {
	struct di_object;
	const char *nonnull id;
	const char *nonnull extname;
	int nsignals;

	uint8_t opcode;

	// ret code: 0 = success, -1 = error, 1 = next
	int (*nullable handle_event)(struct di_xorg_ext *nonnull, xcb_generic_event_t *nonnull ev);
	UT_hash_handle hh;
};

static inline xcb_screen_t *nullable unused screen_of_display(xcb_connection_t *nonnull c,
                                                              int screen) {
	xcb_screen_iterator_t iter;

	iter = xcb_setup_roots_iterator(xcb_get_setup(c));
	for (; iter.rem; --screen, xcb_screen_next(&iter)) {
		if (screen == 0) {
			return iter.data;
		}
	}

	return NULL;
}

static inline bool unused xorg_has_extension(xcb_connection_t *nonnull c,
                                             const char *nonnull name) {
	auto cookie = xcb_list_extensions(c);
	auto r = xcb_list_extensions_reply(c, cookie, NULL);
	if (!r) {
		return false;
	}

	auto i = xcb_list_extensions_names_iterator(r);
	for (; i.rem; xcb_str_next(&i)) {
		if (strncmp(xcb_str_name(i.data), name, xcb_str_name_length(i.data)) == 0) {
			free(r);
			return true;
		}
	}
	free(r);
	return false;
}

#define XORG_CONNECTION_MEMBER "___xorg_connection"
static inline int unused get_xorg_connection(struct di_xorg_ext *nonnull ext,
                                             struct di_xorg_connection *nullable *nonnull dc) {
	return di_get(ext, XORG_CONNECTION_MEMBER, *(struct di_object **)dc);
}

static inline void unused save_xorg_connection(struct di_xorg_ext *nonnull ext,
                                               struct di_xorg_connection *nonnull c) {
	DI_CHECK_OK(di_member_clone(ext, XORG_CONNECTION_MEMBER, (struct di_object *)c));
}

define_object_cleanup(di_xorg_connection);

const struct di_string *nullable di_xorg_get_atom_name(struct di_xorg_connection *nonnull xc,
                                                       xcb_atom_t atom);
xcb_atom_t di_xorg_intern_atom(struct di_xorg_connection *nonnull xc, struct di_string name,
                               xcb_generic_error_t *nullable *nonnull e);

struct di_xorg_ext *nullable new_xinput(struct di_xorg_connection *nonnull);
struct di_xorg_ext *nullable new_randr(struct di_xorg_connection *nonnull);
struct di_xorg_ext *nullable new_key(struct di_xorg_connection *nonnull);
/// Increment the signal count and start fdevent when necessary
void di_xorg_add_signal(struct di_xorg_connection *nonnull);
void di_xorg_ext_signal_setter(const char *nonnull signal, struct di_object *nonnull obj,
                               struct di_object *nonnull sig);
/// Decrement the signal count and stop fdevent when necessary
void di_xorg_del_signal(struct di_xorg_connection *nonnull);
void di_xorg_ext_signal_deleter(const char *nonnull signal, struct di_object *nonnull obj);
