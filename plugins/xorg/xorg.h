#pragma once

#include "list.h"

struct di_xorg {
	struct di_module;

	struct list_head connections;
};

struct di_xorg_connection {
	struct di_object;
	struct di_xorg *x;
	xcb_connection_t *c;
	int dflt_scrn;
	struct di_listener *l;
	struct di_xorg_ext *xi;

	struct list_head siblings;
};

struct di_xorg_ext {
	struct di_object;
	struct di_xorg_connection *dc;
	struct di_xorg_ext **e;
	const char *id;

	uint8_t opcode;

	void (*free)(struct di_xorg_ext *);
	void (*handle_event)(struct di_xorg_ext *, xcb_ge_generic_event_t *ev);
};

static inline xcb_screen_t *screen_of_display(xcb_connection_t *c, int screen) {
	xcb_screen_iterator_t iter;

	iter = xcb_setup_roots_iterator(xcb_get_setup(c));
	for (; iter.rem; --screen, xcb_screen_next(&iter))
		if (screen == 0)
			return iter.data;

	return NULL;
}

static inline bool xorg_has_extension(xcb_connection_t *c, const char *name) {
	auto cookie = xcb_list_extensions(c);
	auto r = xcb_list_extensions_reply(c, cookie, NULL);
	if (!r)
		return false;

	auto i = xcb_list_extensions_names_iterator(r);
	for (; i.rem; xcb_str_next(&i))
		if (strncmp(xcb_str_name(i.data), name, xcb_str_name_length(i.data)) == 0) {
			free(r);
			return true;
		}
	free(r);
	return false;
}

void di_xorg_free_sub(struct di_xorg_ext *x);
