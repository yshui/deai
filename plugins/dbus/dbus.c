#include <assert.h>
#include <stdio.h>

#include <deai/builtin/event.h>
#include <deai/deai.h>
#include <deai/helper.h>
#include <dbus/dbus.h>

#include "common.h"
#include "sedes.h"
#include "yxml.h"

#define DBUS_INTROSPECT_IFACE "org.freedesktop.DBus.Introspectable"

struct di_dbus_connection {
	struct di_object;
	struct deai *di;
	struct di_listener *l;
	DBusConnection *conn;
};

struct di_dbus_object {
	struct di_object;
	char *bus;
	char *obj;
	struct di_dbus_connection *c;
};

struct di_dbus_pending_reply {
	struct di_object;
	struct di_dbus_connection *c;
	DBusPendingCall *p;
};

static void _dbus_pending_call_notify_fn(DBusPendingCall *dp, void *ud) {
	struct di_dbus_pending_reply *p = ud;
	auto msg = dbus_pending_call_steal_reply(dp);
	dbus_pending_call_unref(dp);

	di_emit(p, "reply", (void *)msg);

	// free connection object since we are not going to need it
	di_unref_object((void *)p->c);
	p->c = NULL;
}

static void di_free_pending_reply(struct di_dbus_pending_reply *p) {
	if (p->c)
		di_unref_object((void *)p->c);
}

static struct di_object *di_dbus_send(struct di_dbus_connection *c, DBusMessage *msg) {
	auto ret = di_new_object_with_type(struct di_dbus_pending_reply);
	bool rc = dbus_connection_send_with_reply(c->conn, msg, &ret->p, -1);
	if (!rc) {
		di_unref_object((void *)ret);
		return NULL;
	}
	ret->dtor = (void *)di_free_pending_reply;
	ret->c = c;
	di_ref_object((void *)c);
	di_ref_object((void *)ret);
	dbus_pending_call_set_notify(ret->p, _dbus_pending_call_notify_fn, ret,
	                             (void *)di_unref_object);
	return (void *)ret;
}

static struct di_object *_dbus_introspect(struct di_dbus_object *o) {
	DBusMessage *msg = dbus_message_new_method_call(
	    o->bus, o->obj, DBUS_INTROSPECT_IFACE, "Introspect");
	return di_dbus_send(o->c, msg);
}

static void _dbus_lookup_method_cb(char *method, struct di_object *cb, void *msg) {
	DBusMessageIter i;
	dbus_message_iter_init(msg, &i);
	if (dbus_message_iter_get_arg_type(&i) != DBUS_TYPE_STRING)
		goto err;

	const char *reply;
	dbus_message_iter_get_basic(&i, &reply);

	char xmlstack[4096];
	yxml_t t;
	yxml_init(&t, xmlstack, sizeof(xmlstack));

	char name[256];
	char *current_interface = NULL;
	while (*reply) {
		auto r = yxml_parse(&t, *reply++);
		if (r == YXML_OK)
			continue;
		if (r < YXML_OK)
			goto err;
		switch (r) {
		case YXML_ATTRVAL:
			if (strcmp(t.attr, "name") == 0) {
				yxml_ret_t r2;
				int nlen = 0;
				do {
					int nlen2 = nlen + strlen(t.data);
					if (nlen2 >= 255)
						goto err;
					strcpy(name + nlen, t.data);
					nlen = nlen2;
					r2 = yxml_parse(&t, *reply++);
				} while (r2 != YXML_ATTREND);
			}
			if (strcmp(t.elem, "method") == 0) {
				if (strcmp(name, method) == 0) {
					di_call_callable(cb, current_interface,
					                 (struct di_object *)NULL);
					dbus_message_unref(msg);
					return;
				}
			} else if (strcmp(t.elem, "interface") == 0) {
				if (current_interface)
					goto err;
				current_interface = strdup(name);
			}
			break;
		case YXML_ELEMEND:
			if (strcmp(t.elem, "node") == 0 && current_interface) {
				free(current_interface);
				current_interface = NULL;
			}
			break;
		default:;
		}
	}
err:
	dbus_message_unref(msg);
	di_call_callable(cb, (char *)NULL, di_new_error("Can't find method"));
}

static void _dbus_lookup_method(struct di_dbus_object *o, const char *method,
                                struct di_object *closure) {
	auto p = _dbus_introspect(o);

	auto cl =
	    di_closure(_dbus_lookup_method_cb, false, (method, closure), void *);
	auto l = di_listen_to_once(p, "reply", (void *)cl, true);
	di_unref_object((void *)l);
	di_unref_object((void *)cl);
}

static void _dbus_call_method_reply_cb(struct di_object *sig, void *msg) {
	struct di_tuple t;
	DBusMessageIter i;
	dbus_message_iter_init(msg, &i);
	_dbus_deserialize_tuple(&i, &t);

	di_emitn(sig, "reply", t.length, t.elem_type, (const void **)t.tuple);

	di_free_tuple(t);
	dbus_message_unref(msg);
}

static void
_dbus_call_method_step2(struct di_dbus_object *dobj, struct di_object *sig,
                        char *method, const char *interface, struct di_object *err) {
	if (err)
		di_emit(sig, "error", err);

	DBusMessage *msg =
	    dbus_message_new_method_call(dobj->bus, dobj->obj, interface, method);
	auto p = di_dbus_send(dobj->c, msg);
	auto cl = di_closure(_dbus_call_method_reply_cb, false, (sig), void *);
	auto l = di_listen_to_once(p, "reply", (void *)cl, true);
	di_unref_object((void *)l);
	di_unref_object((void *)cl);
	di_unref_object((void *)p);
}

static void di_free_dbus_object(struct di_object *o) {
	struct di_dbus_object *od = (void *)o;
	free(od->bus);
	free(od->obj);
	di_unref_object((void *)od->c);
}

static struct di_object *
di_dbus_call_method(struct di_dbus_object *dobj, char *method) {
	auto ret = di_new_object_with_type(struct di_object);
	auto cl = di_closure(_dbus_call_method_step2, false,
	                     ((struct di_object *)dobj, ret, method), const char *,
	                     struct di_object *);
	_dbus_lookup_method(dobj, method, (void *)cl);
	di_unref_object((void *)cl);
	return ret;
}

static struct di_object *
di_dbus_object_getter(struct di_dbus_object *dobj, const char *method) {
	return (void *)di_closure(di_dbus_call_method, false,
	                          ((struct di_object *)dobj, method));
}

static struct di_object *
di_dbus_get_object(struct di_object *o, const char *bus, const char *obj) {
	struct di_dbus_connection *oc = (void *)o;
	auto ret = di_new_object_with_type(struct di_dbus_object);
	ret->c = oc;
	di_ref_object((void *)oc);
	ret->bus = strdup(bus);
	ret->obj = strdup(obj);
	di_method(ret, "__get", di_dbus_object_getter, const char *);

	ret->dtor = di_free_dbus_object;
	return (void *)ret;
}

static void ioev_callback(void *conn, void *ptr, int event) {
	if (event & IOEV_READ) {
		dbus_watch_handle(ptr, DBUS_WATCH_READABLE);
		while (dbus_connection_dispatch(conn) != DBUS_DISPATCH_COMPLETE)
			;
	}
	if (event & IOEV_WRITE)
		dbus_watch_handle(ptr, DBUS_WATCH_WRITABLE);
}

static void _di_dbus_shutdown(struct di_dbus_connection *conn) {
	if (!conn->conn)
		return;
	dbus_connection_close(conn->conn);
	dbus_connection_unref(conn->conn);
	conn->conn = NULL;
	di_unref_object((void *)conn->di);
	conn->di = NULL;
}

static void di_dbus_shutdown(struct di_dbus_connection *conn) {
	// this function might be called in dbus dispatch function,
	// closing connection in that context is bad.
	// so delay the shutdown until we return to mainloop
	di_schedule_call(conn->di, _di_dbus_shutdown, ((struct di_object *)conn));
}

static unsigned int _dbus_add_watch(DBusWatch *w, void *ud) {
	struct di_dbus_connection *oc = ud;
	int flags = dbus_watch_get_flags(w);
	int fd = dbus_watch_get_unix_fd(w);
	int dt = 0;
	if (flags & DBUS_WATCH_READABLE)
		dt |= IOEV_READ;
	if (flags & DBUS_WATCH_WRITABLE)
		dt |= IOEV_WRITE;

	struct di_object *ioev;
	di_getm(oc->di, event, false);
	int rc = di_callr(eventm, "fdevent", ioev, fd, dt);
	if (rc != 0)
		return false;

	auto cl = di_closure(ioev_callback, true, ((void *)oc->conn, (void *)w), int);
	auto l = di_listen_to(ioev, "io", (void *)cl);
	di_unref_object((void *)cl);

	if (dbus_watch_get_enabled(w))
		di_call(ioev, "start");
	di_unref_object(ioev);

	dbus_watch_set_data(w, l, (void *)di_stop_listener);

	di_set_detach(l, (void *)_di_dbus_shutdown, (void *)oc);
	return true;
}

static void _dbus_remove_watch(DBusWatch *w, void *ud) {
	struct di_object *l = dbus_watch_get_data(w);
	di_remove_member(l, "__detach");
}

static void _dbus_toggle_watch(DBusWatch *w, void *ud) {
	struct di_object *l = dbus_watch_get_data(w);
	struct di_object *ioev;
	ABRT_IF_ERR(di_get(l, "owner", ioev));
	di_call(ioev, "toggle");
}

static struct di_object *di_dbus_get_session_bus(struct di_object *o) {
	struct di_module *m = (void *)o;
	DBusError e;
	dbus_error_init(&e);

	DBusConnection *conn = dbus_bus_get_private(DBUS_BUS_SESSION, &e);
	if (conn == NULL) {
		auto ret = di_new_error(e.message);
		dbus_error_free(&e);
		return ret;
	}

	dbus_connection_set_exit_on_disconnect(conn, 0);
	auto ret = di_new_object_with_type(struct di_dbus_connection);
	ret->conn = conn;
	ret->di = m->di;
	di_ref_object((void *)m->di);
	di_method(ret, "get", di_dbus_get_object, const char *, const char *);

	dbus_connection_set_watch_functions(conn, _dbus_add_watch, _dbus_remove_watch,
	                                    _dbus_toggle_watch, ret, NULL);

	ret->dtor = (void *)di_dbus_shutdown;

	return (void *)ret;
}

PUBLIC int di_plugin_init(struct deai *di) {
	auto m = di_new_module_with_type(struct di_module);
	m->di = di;

	di_getter(m, session_bus, di_dbus_get_session_bus);

	di_register_module(di, "dbus", (void *)m);
	return 0;
}
