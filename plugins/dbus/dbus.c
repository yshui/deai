#include <assert.h>
#include <stdio.h>

#include <deai/builtins/event.h>
#include <deai/deai.h>
#include <deai/helper.h>
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
} dbus_bus_name;

typedef struct {
	struct di_object;
	DBusConnection *conn;
	struct list_head known_names;
} di_dbus_connection;

typedef struct {
	struct di_object;
	char *bus;
	char *obj;
} di_dbus_object;

typedef struct {
	struct di_object;
	DBusPendingCall *p;
} di_dbus_pending_reply;

static void di_dbus_free_pending_reply(struct di_object *_p) {
	auto p = (di_dbus_pending_reply *)_p;
	// Cancel the reply so the callback won't be called, as the callback would need
	// the pending reply object which we are freeing now.
	dbus_pending_call_cancel(p->p);
	dbus_pending_call_unref(p->p);
}

static void dbus_pending_call_notify_fn(DBusPendingCall *dp, void *ud) {
	di_object_with_cleanup p_obj = di_upgrade_weak_ref(ud);
	auto p = (di_dbus_pending_reply *)p_obj;
	// We made sure this callback won't be called if the pending reply object is not
	// alive.
	DI_CHECK(p != NULL);

	auto msg = dbus_pending_call_steal_reply(dp);

	di_emit(p, "reply", (void *)msg);

	// finalize the object since nothing can happen with it any more
	di_finalize_object(p_obj);
}

static struct di_object *di_dbus_send(di_dbus_connection *c, DBusMessage *msg) {
	auto ret = di_new_object_with_type(di_dbus_pending_reply);
	di_set_type((struct di_object *)ret, "deai.plugin.dbus:DBusPendingReplyRaw");

	bool rc = dbus_connection_send_with_reply(c->conn, msg, &ret->p, -1);
	if (!rc || !ret->p) {
		di_unref_object((void *)ret);
		return NULL;
	}

	di_set_object_dtor((struct di_object *)ret, di_dbus_free_pending_reply);

	dbus_pending_call_set_notify(ret->p, dbus_pending_call_notify_fn,
	                             di_weakly_ref_object((struct di_object *)ret),
	                             (void *)di_drop_weak_ref_rvalue);
	return (void *)ret;
}

static void di_dbus_update_name(di_dbus_connection *c, struct di_string wk,
                                struct di_string unused old, struct di_string unique,
                                bool force_remove) {
	dbus_bus_name *i, *ni;
	if (wk.data[0] == ':') {
		return;
	}

	// remove out dated entries
	// old being empty means this is a new wellknown name, unless force_remove == true
	if (force_remove || old.length > 0) {
		list_for_each_entry_safe (i, ni, &c->known_names, sibling) {
			if (strncmp(wk.data, i->well_known, wk.length) != 0) {
				continue;
			}

			// fprintf(stderr, "remove old %s -> %s\n", wk, i->unique);
			list_del(&i->sibling);
			free(i);
		}
	}
	if (unique.length != 0) {
		// fprintf(stderr, "%s -> %s\n", wk, unique);
		dbus_bus_name *newi =
		    malloc(sizeof(dbus_bus_name) + wk.length + unique.length + 2);
		newi->well_known = newi->buf;
		newi->unique = newi->buf + wk.length + 1;
		newi->unique[-1] = '\0';

		strncpy(newi->well_known, wk.data, wk.length);
		strncpy(newi->unique, unique.data, unique.length);
		newi->unique[unique.length] = '\0';
		list_add(&newi->sibling, &c->known_names);
	}
}

static void di_dbus_update_name_from_msg(struct di_weak_object *weak,
                                         struct di_string busname, DBusMessage *msg) {
	if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_RETURN) {
		return;
	}

	di_object_with_cleanup conn_obj = di_upgrade_weak_ref(weak);
	auto conn = (di_dbus_connection *)conn_obj;
	// the fact we received message must mean the connection is still alive
	DI_CHECK(conn != NULL);

	// Stop listening for GetNameOwner reply
	char *buf;
	asprintf(&buf, "__dbus_watch_%.*s_change_request", (int)busname.length, busname.data);
	DI_CHECK_OK(di_remove_member_raw((struct di_object *)conn, di_string_borrow(buf)));
	free(buf);

	asprintf(&buf, "__dbus_watch_%.*s_change_request_listen_handle",
	         (int)busname.length, busname.data);
	DI_CHECK_OK(di_remove_member_raw((struct di_object *)conn, di_string_borrow(buf)));
	free(buf);

	// Update busname
	DBusError e;
	dbus_error_init(&e);
	const char *unique;
	bool ret = dbus_message_get_args(msg, &e, DBUS_TYPE_STRING, &unique, DBUS_TYPE_INVALID);
	if (!ret) {
		return;
	}
	di_dbus_update_name(conn, busname, DI_STRING_INIT, di_string_borrow(unique), true);
	dbus_message_unref(msg);
}

static void di_dbus_watch_name(di_dbus_connection *c, const char *busname) {
	// watch for name changes
	if (!c->conn) {
		return;
	}

	char *match;
	asprintf(&match,
	         "type='signal',sender='" DBUS_SERVICE_DBUS "',path='" DBUS_PATH_DBUS
	         "',interface='" DBUS_INTERFACE_DBUS "',member='NameOwnerChanged',arg0='"
	         "%s'",
	         busname);

	dbus_bus_add_match(c->conn, match, NULL);
	free(match);

	auto msg = dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
	                                        DBUS_INTERFACE_DBUS, "GetNameOwner");
	dbus_message_append_args(msg, DBUS_TYPE_STRING, &busname, DBUS_TYPE_INVALID);
	auto ret = di_dbus_send(c, msg);
	DI_CHECK(ret != NULL);
	dbus_message_unref(msg);

	di_weak_object_with_cleanup weak = di_weakly_ref_object((struct di_object *)c);
	di_closure_with_cleanup cl = di_closure(
	    di_dbus_update_name_from_msg, (weak, di_string_borrow(busname)), void *);
	auto listen_handle =
	    di_listen_to(ret, di_string_borrow("reply"), (struct di_object *)cl);

	// Keep the listen handle and event source alive
	char *buf;
	asprintf(&buf, "__dbus_watch_%s_change_request", busname);
	di_member(c, buf, ret);
	free(buf);

	asprintf(&buf, "__dbus_watch_%s_change_request_listen_handle", busname);
	di_member(c, buf, listen_handle);
	free(buf);
}

static void di_dbus_unwatch_name(di_dbus_connection *c, const char *busname) {
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

static void dbus_call_method_reply_cb(struct di_weak_object *weak, void *msg) {
	di_object_with_cleanup sig = di_upgrade_weak_ref(weak);
	if (sig == NULL) {
		return;
	}

	struct di_tuple t;
	DBusMessageIter i;
	dbus_message_iter_init(msg, &i);
	_dbus_deserialize_struct(&i, &t);

	if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_RETURN) {
		di_emitn(sig, di_string_borrow("reply"), t);
	} else if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_ERROR) {
		di_emitn(sig, di_string_borrow("error"), t);
	}

	di_free_tuple(t);
	dbus_message_unref(msg);

	// Stop the listener
	di_remove_member_raw(sig, di_string_borrow("___original_object"));
	di_remove_member_raw(sig, di_string_borrow("___original_object_listen_handle"));

	// Drop the dbus connection
	di_remove_member_raw(sig, di_string_borrow("___deai_dbus_connection"));
}

static struct di_object *
dbus_call_method(const char *iface, const char *method, struct di_tuple t) {
	// The first argument is the dbus object
	if (t.length == 0 || t.elements[0].type != DI_TYPE_OBJECT) {
		return di_new_error("first argument to dbus method call is not a dbus "
		                    "object");
	}

	auto dobj = (di_dbus_object *)t.elements[0].value->object;

	di_object_with_cleanup conn = NULL;
	DI_CHECK_OK(di_get(dobj, "___deai_dbus_connection", conn));

	struct di_tuple shifted_args = {
	    .length = t.length - 1,
	    .elements = t.elements + 1,
	};

	// XXX: probably better to destroy all objects when disconnect from bus
	if (conn == NULL) {
		return NULL;
	}

	DBusMessage *msg = dbus_message_new_method_call(dobj->bus, dobj->obj, iface, method);
	DBusMessageIter i;
	dbus_message_iter_init_append(msg, &i);

	if (_dbus_serialize_struct(&i, shifted_args) < 0) {
		return di_new_error("Can't serialize arguments");
	}

	auto ret = di_new_object_with_type(struct di_object);
	di_set_type(ret, "deai.plugin.dbus:DBusPendingReply");

	auto p = di_dbus_send((di_dbus_connection *)conn, msg);
	di_weak_object_with_cleanup weak = di_weakly_ref_object(ret);
	di_closure_with_cleanup cl = di_closure(dbus_call_method_reply_cb, (weak), void *);
	auto listen_handle = di_listen_to(p, di_string_borrow("reply"), (void *)cl);
	dbus_message_unref(msg);

	di_member(ret, "___original_object", p);
	di_member(ret, "___original_object_listen_handle", listen_handle);

	// Keep the dbus connection alive, otherwise we won't get a reply even if the
	// pending reply object is kept alive
	di_member_clone(ret, "___deai_dbus_connection", conn);
	return ret;
}

static void di_free_dbus_object(struct di_object *o) {
	di_dbus_object *od = (void *)o;

	di_object_with_cleanup conn = NULL;
	DI_CHECK_OK(di_get(o, "___deai_dbus_connection", conn));

	di_dbus_unwatch_name((di_dbus_connection *)conn, od->bus);
	free(od->bus);
	free(od->obj);
}

typedef struct {
	struct di_object;
	char *method;
	char *interface;
} di_dbus_method;

static void di_dbus_free_method(struct di_object *o) {
	auto dbus_method = (di_dbus_method *)o;
	free(dbus_method->method);
	free(dbus_method->interface);
}

static int
call_dbus_method(struct di_object *m, di_type_t *rt, union di_value *ret, struct di_tuple t) {
	auto dbus_method = (di_dbus_method *)m;
	*rt = DI_TYPE_OBJECT;
	ret->object = dbus_call_method(dbus_method->interface, dbus_method->method, t);
	return 0;
}

static struct di_object *di_dbus_object_getter(di_dbus_object *dobj, struct di_string method) {
	const char *dot = memrchr(method.data, '.', method.length);
	char *ifc, *m;
	if (dot) {
		const char *dot2 = memchr(method.data, '.', method.length);
		if (dot == method.data || !*(dot + 1)) {
			return di_new_error("Method name or interface name is empty");
		}
		if (dot2 == dot) {
			return di_new_error("Invalid interface name");
		}
		ifc = strndup(method.data, dot - method.data);
		m = strndup(dot + 1, method.length - (dot + 1 - method.data));
	} else {
		ifc = NULL;
		m = di_string_to_chars_alloc(method);
	}

	auto ret = di_new_object_with_type(di_dbus_method);
	di_set_type((struct di_object *)ret, "deai.plugin.dbus:DBusMethod");
	ret->method = m;
	ret->interface = ifc;

	di_set_object_dtor((void *)ret, di_dbus_free_method);
	di_set_object_call((void *)ret, call_dbus_method);
	return (void *)ret;
}

static void di_dbus_object_new_signal(di_dbus_object *dobj, struct di_string name) {
	char *srcsig;
	asprintf(&srcsig, "%%%s%%%s%%%.*s", dobj->bus, dobj->obj, (int)name.length, name.data);

	di_object_with_cleanup conn = NULL;
	DI_CHECK_OK(di_get(dobj, "___deai_dbus_connection", conn));

	di_proxy_signal(conn, di_string_borrow(srcsig), (void *)dobj, name);
	free(srcsig);
}

/// Get a DBus object
///
/// EXPORT: dbus.session_bus.get(destionation: :string, object_path: :string), deai.plugin.dbus:DBusObject
///
/// Create a proxy object for a DBus object. Properties and methods are reflected as
/// members of this object. DBus signals are also converted to signals emitted from this
/// object.
///
/// For how DBus types map to deai type, see :lua:mod:`dbus` for more details.
static struct di_object *
di_dbus_get_object(struct di_object *o, struct di_string bus, struct di_string obj) {
	di_dbus_connection *oc = (void *)o;
	auto ret = di_new_object_with_type(di_dbus_object);
	di_set_type((struct di_object *)ret, "deai.plugin.dbus:DBusObject");

	di_member_clone(ret, "___deai_dbus_connection", (struct di_object *)oc);

	ret->bus = di_string_to_chars_alloc(bus);
	ret->obj = di_string_to_chars_alloc(obj);
	di_dbus_watch_name(oc, ret->bus);
	di_method(ret, "put", di_finalize_object);
	di_method(ret, "__get", di_dbus_object_getter, struct di_string);
	di_method(ret, "__new_signal", di_dbus_object_new_signal, struct di_string);

	di_set_object_dtor((void *)ret, di_free_dbus_object);
	return (void *)ret;
}

static void ioev_callback(void *conn, void *ptr, int event) {
	if (event & IOEV_READ) {
		dbus_watch_handle(ptr, DBUS_WATCH_READABLE);
		while (dbus_connection_dispatch(conn) != DBUS_DISPATCH_COMPLETE) {}
	}
	if (event & IOEV_WRITE) {
		dbus_watch_handle(ptr, DBUS_WATCH_WRITABLE);
	}
}

static void di_dbus_shutdown_part2(
                                   void *root_handle_ptr, DBusConnection *conn) {
	// Stop the listen for "prepare"
	auto root_handle = *(uint64_t *)root_handle_ptr;
	auto roots = di_get_roots();
	DI_CHECK(roots != NULL);
	DI_CHECK_OK(di_call(roots, "__remove_anonymous", root_handle));
	free(root_handle_ptr);

	dbus_connection_close(conn);
	dbus_connection_unref(conn);
}

static void di_dbus_shutdown(di_dbus_connection *conn) {
	if (!conn->conn) {
		return;
	}
	// this function might be called in dbus dispatch function,
	// closing connection in that context is bad.
	// so delay the shutdown until we return to mainloop
	di_object_with_cleanup di = di_object_get_deai_strong((struct di_object *)conn);
	di_object_with_cleanup eventm = NULL;
	DI_CHECK_OK(di_get(di, "event", eventm));

	auto roots = di_get_roots();
	DI_CHECK(roots);
	uint64_t *root_handle_storage = tmalloc(uint64_t, 1);
	di_closure_with_cleanup shutdown =
	    di_closure(di_dbus_shutdown_part2,
	               ((void *)root_handle_storage, (void *)conn->conn));

	di_object_with_cleanup listen_handle =
	    di_listen_to(eventm, di_string_borrow("prepare"), (struct di_object *)shutdown);

	// Keep the listen handle alive as a root. As this is the dtor for `conn`, we
	// cannot keep the handle inside `conn`.
	di_callr(roots, "__add_anonymous", *root_handle_storage, listen_handle);

	// Clear the watch functions so they won't get called. They need the connection
	// object which we are freeing now. And we don't need to be notified about watch
	// removal, as we will destroy the ioev objects with the connection objects.
	dbus_connection_set_watch_functions(conn->conn, NULL, NULL, NULL, NULL, NULL);
	conn->conn = NULL;

	dbus_bus_name *i, *ni;
	list_for_each_entry_safe (i, ni, &conn->known_names, sibling) {
		list_del(&i->sibling);
		free(i);
	}
}

define_trivial_cleanup_t(char);
static unsigned int dbus_add_watch(DBusWatch *w, void *ud) {
	di_dbus_connection *oc = ud;
	unsigned int flags = dbus_watch_get_flags(w);
	int fd = dbus_watch_get_unix_fd(w);
	/*fprintf(stderr, "w %p, flags: %d, fd: %d\n", w, flags, fd);*/
	with_cleanup_t(char) ioev_name;
	asprintf(&ioev_name, "__dbus_ioev_for_watch_%p", w);

	int dt = 0;
	if (flags & DBUS_WATCH_READABLE) {
		dt |= IOEV_READ;
	}
	if (flags & DBUS_WATCH_WRITABLE) {
		dt |= IOEV_WRITE;
	}

	di_object_with_cleanup ioev = NULL;
	if (di_get(oc, ioev_name, ioev) == 0) {
		// We are already watching this fd?
		DI_PANIC("Same watch added multiple times by dbus");
	}

	di_object_with_cleanup eventm = NULL;
	di_object_with_cleanup di = di_object_get_deai_strong((struct di_object *)oc);
	if (di_get(di, "event", eventm) != 0) {
		return false;
	}
	int rc = di_callr(eventm, "fdevent", ioev, fd, dt);
	if (rc != 0) {
		return false;
	}

	auto cl = di_closure(ioev_callback, ((void *)oc->conn, (void *)w), int);
	auto l = di_listen_to(ioev, di_string_borrow("io"), (void *)cl);
	di_unref_object((void *)cl);

	if (!dbus_watch_get_enabled(w)) {
		di_call(ioev, "stop");
	}

	// Keep the listen handle and the ioev
	with_cleanup_t(char) listen_handle_name;
	asprintf(&listen_handle_name, "__dbus_ioev_listen_handle_for_watch_%p", w);
	DI_CHECK_OK(di_member(oc, listen_handle_name, l));
	DI_CHECK_OK(di_member(oc, ioev_name, ioev));

	// Watch cannot hold strong references to the connection object. Otherwise the
	// watches will keep the connection object alive; while the connection object also
	// keeps the watches alive.
	dbus_watch_set_data(w, di_weakly_ref_object((struct di_object *)oc),
	                    (void *)di_drop_weak_ref_rvalue);
	return true;
}

static void dbus_remove_watch(DBusWatch *w, void *ud) {
	di_object_with_cleanup conn = di_upgrade_weak_ref(ud);
	DI_CHECK(conn != NULL);

	with_cleanup_t(char) ioev_name;
	asprintf(&ioev_name, "__dbus_ioev_for_watch_%p", w);
	di_remove_member_raw(conn, di_string_borrow(ioev_name));
	with_cleanup_t(char) listen_handle_name;
	asprintf(&listen_handle_name, "__dbus_ioev_listen_handle_for_watch_%p", w);
	di_remove_member_raw(conn, di_string_borrow(listen_handle_name));
}

static void dbus_toggle_watch(DBusWatch *w, void *ud) {
	struct di_object *oc = dbus_watch_get_data(w);

	with_cleanup_t(char) ioev_name;
	di_object_with_cleanup ioev = NULL;
	asprintf(&ioev_name, "__dbus_ioev_for_watch_%p", w);

	DI_CHECK_OK(di_get(oc, ioev_name, ioev));
	di_call(ioev, "toggle");
}

// connection will emit signal like this:
// <bus name>%<path>%<interface>.<signal name>
static char *to_dbus_match_rule(struct di_string name) {
	const char *sep = memchr(name.data, '%', name.length);
	if (!sep) {
		return NULL;
	}
	const char *sep2 = memchr(sep + 1, '%', name.length - (sep + 1 - name.data));
	if (!sep2 || sep2 == sep + 1) {
		return NULL;
	}
	const char *sep3 = memchr(sep2 + 1, '.', name.length - (sep2 + 1 - name.data));
	if (sep3 == sep2 + 1 || (sep3 && !*(sep3 + 1))) {
		return NULL;
	}

	char *bus = strndup(name.data, sep - name.data);
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

static void di_dbus_new_signal(di_dbus_connection *c, struct di_string name) {
	if (!c->conn) {
		return;
	}

	if (!name.data) {
		return;
	}

	if (*name.data == '%') {
		auto match = to_dbus_match_rule(di_substring_start(name, 1));
		if (!match) {
			return;
		}
		dbus_bus_add_match(c->conn, match, NULL);
		free(match);
	}
}

static void di_dbus_del_signal(di_dbus_connection *c, struct di_string name) {
	if (!c->conn) {
		return;
	}
	if (!name.data) {
		return;
	}

	if (*name.data == '%') {
		auto match = to_dbus_match_rule(di_substring_start(name, 1));
		if (!match) {
			return;
		}
		dbus_bus_remove_match(c->conn, match, NULL);
		free(match);
	}
}

static DBusHandlerResult dbus_filter(DBusConnection *conn, DBusMessage *msg, void *ud) {
	if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_SIGNAL) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	DBusMessageIter i;
	dbus_message_iter_init(msg, &i);

	struct di_tuple t;
	_dbus_deserialize_struct(&i, &t);

	di_dbus_connection *c = ud;
	if (strcmp(dbus_message_get_member(msg), "NameOwnerChanged") == 0 &&
	    strcmp(dbus_message_get_sender(msg), DBUS_SERVICE_DBUS) == 0 &&
	    strcmp(dbus_message_get_path(msg), DBUS_PATH_DBUS) == 0 &&
	    strcmp(dbus_message_get_interface(msg), DBUS_INTERFACE_DBUS) == 0) {
		// Handle name change
		auto wk = t.elements[0].value->string;
		auto old = t.elements[1].value->string;
		auto new = t.elements[2].value->string;
		di_dbus_update_name(c, wk, old, new, false);
	}

	char *sig;

	// Prevent connection object from dying during signal emission
	di_ref_object(ud);
	auto bus_name = dbus_message_get_sender(msg);
	auto path = dbus_message_get_path(msg);
	auto ifc = dbus_message_get_interface(msg);
	auto mbr = dbus_message_get_member(msg);

	if (!bus_name) {
		di_unref_object(ud);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (*bus_name != ':') {
		// We got a well known name
		asprintf(&sig, "%%%s%%%s%%%s.%s", bus_name, path, ifc, mbr);
		di_emitn(ud, di_string_borrow(sig), t);
		free(sig);
		// Emit the interface-less version of the signal
		asprintf(&sig, "%%%s%%%s%%%s", bus_name, path, mbr);
		di_emitn(ud, di_string_borrow(sig), t);
		free(sig);
	} else {
		dbus_bus_name *ni;
		list_for_each_entry (ni, &c->known_names, sibling) {
			if (strcmp(ni->unique, bus_name) != 0) {
				continue;
			}
			asprintf(&sig, "%%%s%%%s%%%s.%s", ni->well_known, path, ifc, mbr);
			di_emitn(ud, di_string_borrow(sig), t);
			free(sig);
			// Emit the interface-less version of the signal
			asprintf(&sig, "%%%s%%%s%%%s", ni->well_known, path, mbr);
			di_emitn(ud, di_string_borrow(sig), t);
			free(sig);
		}
	}
	di_free_value(DI_TYPE_TUPLE, (union di_value *)&t);
	di_unref_object(ud);
	return DBUS_HANDLER_RESULT_HANDLED;
}

/// DBus session bus
///
/// EXPORT: dbus.session_bus, deai.plugin.dbus:DBusConnection
///
/// A connection to the DBus session bus.
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

	auto di = di_module_get_deai(m);
	if (di == NULL) {
		return di_new_error("deai is shutting down...");
	}

	dbus_connection_set_exit_on_disconnect(conn, 0);
	auto ret = di_new_object_with_type(di_dbus_connection);
	di_set_type((struct di_object *)ret, "deai.plugin.dbus:DBusConnection");

	ret->conn = conn;
	di_member(ret, DEAI_MEMBER_NAME_RAW, di);
	INIT_LIST_HEAD(&ret->known_names);
	di_method(ret, "get", di_dbus_get_object, struct di_string, struct di_string);
	di_method(ret, "__new_signal", di_dbus_new_signal, struct di_string);
	di_method(ret, "__del_signal", di_dbus_del_signal, struct di_string);

	dbus_connection_set_watch_functions(conn, dbus_add_watch, dbus_remove_watch,
	                                    dbus_toggle_watch, ret, NULL);

	dbus_connection_add_filter(conn, dbus_filter, ret, NULL);

	di_set_object_dtor((void *)ret, (void *)di_dbus_shutdown);

	return (void *)ret;
}

/// D-Bus
///
/// EXPORT: dbus, deai:module
///
/// **D-Bus types**
///
/// D-Bus Types are converted to/from deai types when reading a DBus property, receiving a
/// DBus signal, or when calling a DBus method.
///
/// Going from DBus to deai is straightforward, all signed integers becomes integer, all
/// unsigned integers becomes unsigned integer. Arrays become arrays. Structs become array
/// of variants. Unix FD is not supported currently.
///
/// Going from deai to DBus is harder. Because deai doesn't have multiple integer types,
/// it only uses DBus INT32 and UINT32, so sometimes calling a method could fail. To have
/// complete support, we need to call Introspect and parse the XML to get the method
/// signature. This is not yet implemented.
///
struct di_module *new_dbus_module(struct deai *di) {
	auto m = di_new_module(di);
	di_getter(m, session_bus, di_dbus_get_session_bus);
	return m;
}

DEAI_PLUGIN_ENTRY_POINT(di) {
	auto m = new_dbus_module(di);
	di_register_module(di, di_string_borrow("dbus"), &m);
	return 0;
}
