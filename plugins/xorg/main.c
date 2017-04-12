#include <deai.h>
#include <event.h>
#include <log.h>
#include <plugin.h>

#include <xcb/xcb.h>
#include <xcb/xinput.h>

#include "utils.h"
#include "uthash.h"
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

struct di_xorg_xinput {
	struct di_xorg_ext;

	xcb_input_event_mask_t ec;
	char mask[4];
};

#define set_mask(a, m) (a)[(m)>>3] |= (1<<((m)&7))
#define clear_mask(a, m) (a)[(m)>>3] &= ~(1<<((m)&7))
#define get_mask(a, m) (a)[(m)>>3] & (1<<((m)&7))

static xcb_screen_t *screen_of_display(xcb_connection_t *c, int screen) {
	xcb_screen_iterator_t iter;

	iter = xcb_setup_roots_iterator(xcb_get_setup(c));
	for (; iter.rem; --screen, xcb_screen_next(&iter))
		if (screen == 0)
			return iter.data;

	return NULL;
}

static void di_xorg_ioev(struct di_listener_data *l) {
	struct di_xorg_connection *dc = (void *)l->user_data;
	module_cleanup struct di_module *log = di_find_module(dc->x->di, "log");
	di_log_va((void *)log, DI_LOG_ERROR, "xcb ioev\n");

	auto ev = xcb_poll_for_event(dc->c);
	// handle event

	if (ev->response_type == XCB_GE_GENERIC) {
		auto gev = (xcb_ge_generic_event_t *)ev;
		if (gev->extension == dc->xi->opcode)
			dc->xi->handle_event(dc->xi, gev);
	}
}

static void di_xorg_free_sub(struct di_xorg_ext *x) {
	*(x->e) = NULL;
	x->free(x);
}

static void di_xorg_listen_for_new_device(struct di_xorg_xinput *xi) {
	module_cleanup struct di_module *log = di_find_module(xi->dc->x->di, "log");
	auto scrn = screen_of_display(xi->dc->c, xi->dc->dflt_scrn);
	set_mask(xi->mask, XCB_INPUT_DEVICE_CHANGED);
	auto cookie = xcb_input_xi_select_events_checked(xi->dc->c, scrn->root, 1, &xi->ec);
	auto e = xcb_request_check(xi->dc->c, cookie);
	if (e)
		di_log_va(log, DI_LOG_ERROR, "select events failed\n");
}

static void di_xorg_stop_listen_for_new_device(struct di_xorg_xinput *xi) {
	auto scrn = screen_of_display(xi->dc->c, xi->dc->dflt_scrn);
	clear_mask(xi->mask, XCB_INPUT_DEVICE_CHANGED);
	xcb_input_xi_select_events_checked(xi->dc->c, scrn->root, 1, &xi->ec);
}

static void di_xorg_free_xinput(struct di_xorg_ext *x) {
	// clear event mask when free
	struct di_xorg_xinput *xi = (void *)x;
	memset(xi->mask, 0, xi->ec.mask_len*4);
	auto scrn = screen_of_display(xi->dc->c, xi->dc->dflt_scrn);
	xcb_input_xi_select_events_checked(xi->dc->c, scrn->root, 1, &xi->ec);
}

static void di_xorg_handle_xinput_event(struct di_xorg_xinput *xi, xcb_ge_generic_event_t *ev) {
	if (ev->event_type == XCB_INPUT_DEVICE_CHANGED) {
		di_emit_signal_v((void *)xi, "new-device");
	}
}

static bool xorg_has_extension(xcb_connection_t *c, const char *name) {
	auto cookie = xcb_list_extensions(c);
	auto r = xcb_list_extensions_reply(c, cookie, NULL);
	if (!r)
		return false;

	auto i = xcb_list_extensions_names_iterator(r);
	for (; i.rem; xcb_str_next(&i))
		if (strncmp(xcb_str_name(i.data), name, xcb_str_name_length(i.data)) == 0)
			return true;
	return false;
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
	xi->ec.mask_len = 1; // 4 bytes unit
	xi->ec.deviceid = 0; //alldevice
	xi->opcode = r->major_opcode;
	xi->handle_event = (void *)di_xorg_handle_xinput_event;
	dc->xi = (void *)xi;
	xi->dc = dc;

	free(r);

	dc->xi->free = di_xorg_free_xinput;

	auto tm = di_create_typed_method((di_fn_t)di_xorg_free_sub, "__dtor",
	                                 DI_TYPE_VOID, 0);
	di_register_typed_method((void *)xi, tm);

	tm = di_create_typed_method((di_fn_t)di_xorg_listen_for_new_device,
	                            "__add_listener_new-device", DI_TYPE_VOID, 0);
	di_register_typed_method((void *)xi, tm);

	tm = di_create_typed_method((di_fn_t)di_xorg_stop_listen_for_new_device,
	                            "__del_listener_new-device", DI_TYPE_VOID, 0);
	di_register_typed_method((void *)xi, tm);

	di_register_signal((void *)xi, "new-device", 0);
	return (void *)dc->xi;
}

static struct di_object *di_xorg_connect(struct di_xorg *x) {
	struct di_xorg_connection *dc =
	    di_new_object_with_type(struct di_xorg_connection);
	dc->c = xcb_connect(NULL, &dc->dflt_scrn);

	struct di_module *evm = di_find_module(x->di, "event");
	if (!evm)
		return NULL;

	struct di_method *m = di_find_method((void *)evm, "fdevent");
	void *ret;
	di_type_t rtype;
	di_call_callable_v((void *)m, &rtype, &ret, DI_TYPE_NINT,
	                   xcb_get_file_descriptor(dc->c), DI_TYPE_NINT, IOEV_READ,
	                   DI_LAST_TYPE);

	struct di_object *xcbfd = *(struct di_object **)ret;
	free(ret);

	dc->l = di_add_typed_listener(xcbfd, "read", dc, (di_fn_t)di_xorg_ioev);
	di_unref_object(xcbfd);        // kept alive by the listener

	m = di_find_method(xcbfd, "start");
	di_call_callable_v((void *)m, &rtype, &ret, DI_LAST_TYPE);

	auto tm = di_create_typed_method((di_fn_t)di_xorg_get_xinput, "__get_xinput",
	                                 DI_TYPE_OBJECT, 0);
	di_register_typed_method((void *)dc, tm);

	dc->x = x;
	list_add(&dc->siblings, &x->connections);

	return (void *)dc;
}
PUBLIC int di_plugin_init(struct deai *di) {
	struct di_xorg *x = di_new_module_with_type("xorg", struct di_xorg);
	x->di = di;

	struct di_typed_method *conn = di_create_typed_method(
	    (di_fn_t)di_xorg_connect, "connect", DI_TYPE_OBJECT, 0);
	di_register_typed_method((void *)x, conn);

	INIT_LIST_HEAD(&x->connections);

	di_register_module(di, (void *)x);
	return 0;
}
