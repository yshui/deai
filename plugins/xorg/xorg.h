/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

#include <deai/deai.h>

#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon.h>

#include "uthash.h"
#include "utils.h"

struct di_xorg;

struct di_atom_entry;
struct di_xorg_ext;

struct di_xorg_connection {
	struct di_object;
	struct di_xorg *x;
	xcb_connection_t *c;
	int dflt_scrn;
	struct di_xorg_ext *xext;

	struct xkb_context *xkb_ctx;

	struct di_atom_entry *a_byatom, *a_byname;
};

struct di_xorg_ext {
	struct di_object;
	struct di_xorg_connection *dc;
	const char *id;
	const char *extname;

	uint8_t opcode;

	void (*free)(struct di_xorg_ext *);

	// ret code: 0 = success, -1 = error, 1 = next
	int (*handle_event)(struct di_xorg_ext *, xcb_generic_event_t *ev);
	UT_hash_handle hh;
};

static inline xcb_screen_t *unused screen_of_display(xcb_connection_t *c, int screen) {
	xcb_screen_iterator_t iter;

	iter = xcb_setup_roots_iterator(xcb_get_setup(c));
	for (; iter.rem; --screen, xcb_screen_next(&iter)) {
		if (screen == 0) {
			return iter.data;
		}
	}

	return NULL;
}

static inline bool unused xorg_has_extension(xcb_connection_t *c, const char *name) {
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

const struct di_string *di_xorg_get_atom_name(struct di_xorg_connection *xc, xcb_atom_t atom);
xcb_atom_t di_xorg_intern_atom(struct di_xorg_connection *xc, struct di_string name,
                               xcb_generic_error_t **e);

struct di_xorg_ext *new_xinput(struct di_xorg_connection *);
struct di_xorg_ext *new_randr(struct di_xorg_connection *);
struct di_xorg_ext *new_key(struct di_xorg_connection *);
