#include <assert.h>
#include <stdio.h>

#include <deai/builtin/event.h>
#include <deai/deai.h>
#include <deai/helper.h>
#include <deai/module.h>
#include <dbus/dbus.h>

#include "common.h"
#include "list.h"
#include "sedes.h"

#define DBUS_INTROSPECT_IFACE "org.freedesktop.DBus.Introspectable"

typedef struct {
	struct list_head sibling;
	char *well_known;
	char *unique;
	char buf[];
} _bus_name;

typedef struct {
	struct di_object;
	struct deai *di;
	struct di_listener *l;
	DBusConnection *conn;
	struct list_head known_names;
} _di_dbus_connection;

typedef struct {
	struct di_object;
	char *bus;
	char *obj;
	_di_dbus_connection *c;
} _di_dbus_object;

typedef struct {
	struct di_object;
	_di_dbus_connection *c;
	DBusPendingCall *p;
} _di_dbus_pending_reply;

static void _dbus_pending_call_notify_fn(DBusPendingCall *dp, void *ud) {
	_di_dbus_pending_reply *p = ud;
	auto msg = dbus_pending_call_steal_reply(dp);
	dbus_pending_call_unref(dp);

	di_emit(p, "reply", (void *)msg);

	// free connection object since we are not going to need it
	di_destroy_object((void *)p);
}

static void di_free_pending_reply(_di_dbus_pending_reply *p) {
	di_unref_object((void *)p->c);
}

static struct di_object *di_dbus_send(_di_dbus_connection *c, DBusMessage *msg) {
	auto ret = di_new_object_with_type(_di_dbus_pending_reply);
	bool rc = dbus_connection_send_with_reply(c->conn, msg, &ret->p, -1);
	if (!rc) {
		di_unref_object((void *)ret);
		return NULL;
	}
	di_set_object_dtor((void *)ret, (void *)di_free_pending_reply);
	ret->c = c;
	di_ref_object((void *)c);
	di_ref_object((void *)ret);
	dbus_pending_call_set_notify(ret->p, _dbus_pending_call_notify_fn, ret,
	                             (void *)di_unref_object);
	return (void *)ret;
}

static void di_dbus_update_name(_di_dbus_connection *c, const char *wk, const char *old,
                                const char *unique) {
	_bus_name *i, *ni;
	if (*wk == ':') {
		return;
	}

	// if old == NULL we will walk the list anyway
	if (!old || *old != '\0') {
		// remove out dated entries
		list_for_each_entry_safe (i, ni, &c->known_names, sibling) {
			if (strcmp(wk, i->well_known) != 0) {
				continue;
			}

			// fprintf(stderr, "remove old %s -> %s\n", wk, i->unique);
			list_del(&i->sibling);
			free(i);
		}
	}
	if (*unique != '\0') {
		// fprintf(stderr, "%s -> %s\n", wk, unique);
		auto wklen = strlen(wk);
		_bus_name *newi = malloc(sizeof(_bus_name) + wklen + strlen(unique) + 2);
		newi->well_known = newi->buf;
		newi->unique = newi->buf + wklen + 1;
		strcpy(newi->well_known, wk);
		strcpy(newi->unique, unique);
		list_add(&newi->sibling, &c->known_names);
	}
}

static void di_dbus_update_name_from_msg(_di_dbus_connection *c, char *wk, DBusMessage *msg) {
	if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_RETURN) {
		return;
	}

	DBusError e;
	dbus_error_init(&e);
	const char *unique;
	bool ret = dbus_message_get_args(msg, &e, DBUS_TYPE_STRING, &unique, DBUS_TYPE_INVALID);
	if (!ret) {
		return;
	}
	di_dbus_update_name(c, wk, NULL, unique);
	dbus_message_unref(msg);
}

static void di_dbus_watch_name(_di_dbus_connection *c, const char *busname) {
	// watch for name changes
	if (!c->conn) {
		return;
	}

	char *match;
	asprintf(&match,
	         "type='signal',sender='" DBUS_SERVICE_DBUS "',path='" DBUS_PATH_DBUS
	         "',interface='" DBUS_INTERFACE_DBUS "',member='NameOwnerChanged',arg0='%"
	         "s'",
	         busname);

	dbus_bus_add_match(c->conn, match, NULL);
	free(match);

	auto msg = dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
	                                        DBUS_INTERFACE_DBUS, "GetNameOwner");
	dbus_message_append_args(msg, DBUS_TYPE_STRING, &busname, DBUS_TYPE_INVALID);
	auto ret = di_dbus_send(c, msg);
	dbus_message_unref(msg);

	// Cast busname to "char *" so ri_closure would clone it.
	auto cl =
	    di_closure(di_dbus_update_name_from_msg, ((void *)c, (char *)busname), void *);
	di_listen_to_once(ret, "reply", (struct di_object *)cl, true);
	di_unref_object((void *)cl);
	di_unref_object((void *)ret);
}

static void di_dbus_unwatch_name(_di_dbus_connection *c, const char *busname) {
	// stop watching for name changes
	if (!c->conn) {
		return;
	}

	char *match;
	asprintf(&match,
	         "type='signal',sender='" DBUS_SERVICE_DBUS "',path='" DBUS_PATH_DBUS
	         "',interface='" DBUS_INTERFACE_DBUS "',member='NameOwnerChanged',arg0='%"
	         "s'",
	         busname);

	dbus_bus_remove_match(c->conn, match, NULL);
	free(match);
}

#if 0
static struct di_object *_dbus_introspect(_di_dbus_object *o) {
	DBusMessage *msg = dbus_message_new_method_call(
	    o->bus, o->obj, DBUS_INTROSPECT_IFACE, "Introspect");
	auto ret = di_dbus_send(o->c, msg);
	dbus_message_unref(msg);
	return ret;
}

static void _dbus_lookup_member_cb(char *method, bool is_signal,
                                   struct di_object *cb, void *msg) {
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
	const char *mtype = is_signal ? "signal" : "method";
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
			//fprintf(stderr, "%s: %s\n", t.elem, name);
			if (strcmp(t.elem, mtype) == 0) {
				if (strcmp(name, method) == 0) {
					di_call_callable(cb, current_interface,
					                 (struct di_object *)NULL);
					free(current_interface);
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

static void _dbus_lookup_member(_di_dbus_object *o, const char *method,
                                bool is_signal, struct di_object *closure) {
	auto p = _dbus_introspect(o);

	auto cl = di_closure(_dbus_lookup_member_cb,
	                     (method, is_signal, closure), void *);
	di_listen_to_once(p, "reply", (void *)cl, true);
	di_unref_object((void *)cl);
	di_unref_object(p);
}
#endif

static void _dbus_call_method_reply_cb(struct di_object *sig, void *msg) {
	struct di_tuple t;
	DBusMessageIter i;
	dbus_message_iter_init(msg, &i);
	_dbus_deserialize_struct(&i, &t);

	if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_RETURN) {
		di_emitn(sig, "reply", t);
	} else if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_ERROR) {
		di_emitn(sig, "error", t);
	}

	di_free_tuple(t);
	dbus_message_unref(msg);

	di_destroy_object(sig);
}

static struct di_object *
_dbus_call_method(const char *iface, const char *method, struct di_tuple t) {
	// The first argument is the dbus object
	if (t.length == 0 || t.elements[0].type != DI_TYPE_OBJECT) {
		return di_new_error("first argument to dbus method call is not a dbus "
		                    "object");
	}

	auto dobj = (_di_dbus_object *)t.elements[0].value->object;

	struct di_tuple shifted_args = {
	    .length = t.length - 1,
	    .elements = t.elements + 1,
	};

	// XXX: probably better to destroy all objects when disconnect from bus
	if (!dobj->c->conn) {
		return NULL;
	}

	DBusMessage *msg = dbus_message_new_method_call(dobj->bus, dobj->obj, iface, method);
	DBusMessageIter i;
	dbus_message_iter_init_append(msg, &i);

	if (_dbus_serialize_struct(&i, shifted_args) < 0) {
		return di_new_error("Can't serialize arguments");
	}

	auto ret = di_new_object_with_type(struct di_object);
	auto p = di_dbus_send(dobj->c, msg);
	auto cl = di_closure(_dbus_call_method_reply_cb, (ret), void *);
	di_listen_to_once(p, "reply", (void *)cl, true);
	di_unref_object((void *)cl);
	di_unref_object((void *)p);
	dbus_message_unref(msg);
	return ret;
}

static void di_free_dbus_object(struct di_object *o) {
	_di_dbus_object *od = (void *)o;
	di_dbus_unwatch_name(od->c, od->bus);
	free(od->bus);
	free(od->obj);
	di_unref_object((void *)od->c);
}

typedef struct {
	struct di_object;
	char *method;
	char *interface;
} _di_dbus_method;

static void di_dbus_free_method(struct di_object *o) {
	auto dbus_method = (_di_dbus_method *)o;
	free(dbus_method->method);
	free(dbus_method->interface);
}

static int
call_dbus_method(struct di_object *m, di_type_t *rt, union di_value *ret, struct di_tuple t) {
	auto dbus_method = (_di_dbus_method *)m;
	*rt = DI_TYPE_OBJECT;
	ret->object = _dbus_call_method(dbus_method->interface, dbus_method->method, t);
	return 0;
}

static struct di_object *di_dbus_object_getter(_di_dbus_object *dobj, const char *method) {
	const char *dot = strrchr(method, '.');
	char *ifc, *m;
	if (dot) {
		const char *dot2 = strchr(method, '.');
		if (dot == method || !*(dot + 1)) {
			return di_new_error("Method name or interface name is empty");
		}
		if (dot2 == dot) {
			return di_new_error("Invalid interface name");
		}
		ifc = strndup(method, dot - method);
		m = strdup(dot + 1);
	} else {
		ifc = NULL;
		m = strdup(method);
	}

	auto ret = di_new_object_with_type(_di_dbus_method);
	ret->method = m;
	ret->interface = ifc;
	di_ref_object((void *)dobj);

	di_set_object_dtor((void *)ret, di_dbus_free_method);
	di_set_object_call((void *)ret, call_dbus_method);
	return (void *)ret;
}

static void di_dbus_object_new_signal(_di_dbus_object *dobj, const char *name) {
	char *srcsig;
	asprintf(&srcsig, "%%%s%%%s%%%s", dobj->bus, dobj->obj, name);
	di_proxy_signal((void *)dobj->c, srcsig, (void *)dobj, name);
	free(srcsig);
}

static struct di_object *
di_dbus_get_object(struct di_object *o, const char *bus, const char *obj) {
	_di_dbus_connection *oc = (void *)o;
	auto ret = di_new_object_with_type(_di_dbus_object);
	ret->c = oc;
	di_ref_object((void *)oc);
	di_dbus_watch_name(oc, bus);
	ret->bus = strdup(bus);
	ret->obj = strdup(obj);
	di_method(ret, "put", di_destroy_object);
	di_method(ret, "__get", di_dbus_object_getter, const char *);
	di_method(ret, "__new_signal", di_dbus_object_new_signal, const char *);

	di_set_object_dtor((void *)ret, di_free_dbus_object);
	return (void *)ret;
}

static void ioev_callback(void *conn, void *ptr, int event) {
	if (event & IOEV_READ) {
		dbus_watch_handle(ptr, DBUS_WATCH_READABLE);
		while (dbus_connection_dispatch(conn) != DBUS_DISPATCH_COMPLETE) {
		}
	}
	if (event & IOEV_WRITE) {
		dbus_watch_handle(ptr, DBUS_WATCH_WRITABLE);
	}
}

static void _dbus_shutdown(DBusConnection *conn) {
	dbus_connection_close(conn);
	dbus_connection_unref(conn);
}

static void di_dbus_shutdown(_di_dbus_connection *conn) {
	if (!conn->conn) {
		return;
	}
	// this function might be called in dbus dispatch function,
	// closing connection in that context is bad.
	// so delay the shutdown until we return to mainloop
	di_schedule_call(conn->di, _dbus_shutdown, ((void *)conn->conn));
	conn->conn = NULL;
	di_unref_object((void *)conn->di);
	conn->di = NULL;

	_bus_name *i, *ni;
	list_for_each_entry_safe (i, ni, &conn->known_names, sibling) {
		list_del(&i->sibling);
		free(i);
	}
}

static unsigned int _dbus_add_watch(DBusWatch *w, void *ud) {
	_di_dbus_connection *oc = ud;
	unsigned int flags = dbus_watch_get_flags(w);
	int fd = dbus_watch_get_unix_fd(w);
	int dt = 0;
	if (flags & DBUS_WATCH_READABLE) {
		dt |= IOEV_READ;
	}
	if (flags & DBUS_WATCH_WRITABLE) {
		dt |= IOEV_WRITE;
	}

	struct di_object *ioev;
	di_getm(oc->di, event, false);
	int rc = di_callr(eventm, "fdevent", ioev, fd, dt);
	if (rc != 0) {
		return false;
	}

	auto cl = di_closure(ioev_callback, ((void *)oc->conn, (void *)w), int);
	auto l = di_listen_to(ioev, "io", (void *)cl);
	di_unref_object((void *)cl);

	if (!dbus_watch_get_enabled(w)) {
		di_call(ioev, "stop");
	}
	di_unref_object(ioev);

	dbus_watch_set_data(w, l, (void *)di_stop_listener);

	return true;
}

static void _dbus_toggle_watch(DBusWatch *w, void *ud) {
	struct di_object *l = dbus_watch_get_data(w);
	struct di_object *ioev;
	ABRT_IF_ERR(di_get(l, "owner", ioev));
	di_call(ioev, "toggle");
}

// connection will emit signal like this:
// <bus name>%<path>%<interface>.<signal name>
static char *_to_dbus_match_rule(const char *name) {
	const char *sep = strchr(name, '%');
	if (!sep) {
		return NULL;
	}
	const char *sep2 = strchr(sep + 1, '%');
	if (!sep2 || sep2 == sep + 1) {
		return NULL;
	}
	const char *sep3 = strrchr(sep2 + 1, '.');
	if (sep3 == sep2 + 1 || (sep3 && !*(sep3 + 1))) {
		return NULL;
	}

	char *bus = strndup(name, sep - name);
	char *path = strndup(sep + 1, sep2 - sep - 1);
	char *interface = NULL;
	if (sep3) {
		interface = strndup(sep2 + 1, sep3 - sep2 - 1);
	}
	const char *signal = sep3 ? sep3 + 1 : sep2 + 1;

	char *match;
	if (interface) {
		asprintf(&match,
		         "type='signal',sender='%s',path='%s',interface='%s'"
		         ",member='%s'",
		         bus, path, interface, signal);
	} else {
		asprintf(&match, "type='signal',sender='%s',path='%s',member='%s'", bus,
		         path, signal);
	}

	free(bus);
	free(path);
	free(interface);
	return match;
}

static void di_dbus_new_signal(_di_dbus_connection *c, const char *name) {
	if (!c->conn) {
		return;
	}

	if (*name == '%') {
		auto match = _to_dbus_match_rule(name + 1);
		if (!match) {
			return;
		}
		dbus_bus_add_match(c->conn, match, NULL);
		free(match);
	}
}

static void di_dbus_del_signal(_di_dbus_connection *c, const char *name) {
	if (!c->conn) {
		return;
	}

	if (*name == '%') {
		auto match = _to_dbus_match_rule(name + 1);
		if (!match) {
			return;
		}
		dbus_bus_remove_match(c->conn, match, NULL);
		free(match);
	}
}

static DBusHandlerResult _dbus_filter(DBusConnection *conn, DBusMessage *msg, void *ud) {
	if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_SIGNAL) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	DBusMessageIter i;
	dbus_message_iter_init(msg, &i);

	struct di_tuple t;
	_dbus_deserialize_struct(&i, &t);

	_di_dbus_connection *c = ud;
	if (strcmp(dbus_message_get_member(msg), "NameOwnerChanged") == 0 &&
	    strcmp(dbus_message_get_sender(msg), DBUS_SERVICE_DBUS) == 0 &&
	    strcmp(dbus_message_get_path(msg), DBUS_PATH_DBUS) == 0 &&
	    strcmp(dbus_message_get_interface(msg), DBUS_INTERFACE_DBUS) == 0) {
		// Handle name change
		auto wk = t.elements[0].value->string;
		auto old = t.elements[1].value->string;
		auto new = t.elements[2].value->string;
		di_dbus_update_name(c, wk, old, new);
	}

	char *sig;

	// Prevent connection object from dying during signal emission
	di_ref_object(ud);
	auto bus_name = dbus_message_get_sender(msg);
	auto path = dbus_message_get_path(msg);
	auto ifc = dbus_message_get_interface(msg);
	auto mbr = dbus_message_get_member(msg);

	if (!bus_name) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (*bus_name != ':') {
		// We got a well known name
		asprintf(&sig, "%%%s%%%s%%%s.%s", bus_name, path, ifc, mbr);
		di_emitn(ud, sig, t);
		free(sig);
		// Emit the interface-less version of the signal
		asprintf(&sig, "%%%s%%%s%%%s", bus_name, path, mbr);
		di_emitn(ud, sig, t);
		free(sig);
	} else {
		_bus_name *ni;
		list_for_each_entry (ni, &c->known_names, sibling) {
			if (strcmp(ni->unique, bus_name) != 0) {
				continue;
			}
			asprintf(&sig, "%%%s%%%s%%%s.%s", ni->well_known, path, ifc, mbr);
			di_emitn(ud, sig, t);
			free(sig);
			// Emit the interface-less version of the signal
			asprintf(&sig, "%%%s%%%s%%%s", ni->well_known, path, mbr);
			di_emitn(ud, sig, t);
			free(sig);
		}
	}
	di_free_value(DI_TYPE_TUPLE, (union di_value *)&t);
	di_unref_object(ud);
	return DBUS_HANDLER_RESULT_HANDLED;
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
	auto ret = di_new_object_with_type(_di_dbus_connection);
	ret->conn = conn;
	ret->di = di_module_get_deai(m);
	INIT_LIST_HEAD(&ret->known_names);
	di_ref_object((void *)ret->di);
	di_method(ret, "get", di_dbus_get_object, const char *, const char *);
	di_method(ret, "__new_signal", di_dbus_new_signal, const char *);
	di_method(ret, "__del_signal", di_dbus_del_signal, const char *);

	dbus_connection_set_watch_functions(conn, _dbus_add_watch, NULL,
	                                    _dbus_toggle_watch, ret, NULL);

	dbus_connection_add_filter(conn, _dbus_filter, ret, NULL);

	di_set_object_dtor((void *)ret, (void *)di_dbus_shutdown);

	return (void *)ret;
}

PUBLIC int di_plugin_init(struct deai *di) {
	auto m = di_new_module(di);

	di_getter(m, session_bus, di_dbus_get_session_bus);

	di_register_module(di, "dbus", &m);
	return 0;
}
