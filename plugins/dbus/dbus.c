#include <stdio.h>

#include <deai/builtins/event.h>
#include <deai/builtins/log.h>
#include <deai/deai.h>
#include <deai/error.h>
#include <deai/helper.h>
#include <dbus/dbus.h>
#include <uthash.h>

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

// Objects are cached in the connection object. The connection objects have members like:
//
//     "object_cache_<well-known name>" -> object cache directory object
//     "peer_<unique name>" -> dbus unique name objects
//
// A dbus object cache object contains dbus object proxies available from that well-known
// name, indexed by the object path.
//
// A dbus unique name object contains dbus object cache objects, indexed by their
// well-known name.
//
// dbus object cache objects are moved around to the correct unique name objects based on
// NameOwnerChanged signals from dbus.
//
// objects hold strong references to connection and the directory object. directory
// objects hold strong references to the unique name objects. This is so that objects can
// keep the caches alive. References from connection to objects, directory to objects, and
// unique name to directories are weak.
typedef struct {
	di_object;
	DBusConnection *conn;
	/// Count the number of in-flight dbus calls or registered signal handlers. When
	/// This reaches zero, we will stop watching the dbus file descriptor; when this
	/// becomes non-zero, we will start watching the dbus file descriptor.
	int nsignals;
} di_dbus_connection;

typedef struct {
	di_object;
} di_dbus_object;

typedef struct {
	di_object;
	DBusPendingCall *p;
	DBusMessage *msg;
} di_dbus_pending_reply;

#if 0
static di_object *_dbus_introspect(_di_dbus_object *o) {
	DBusMessage *msg = dbus_message_new_method_call(
	    o->bus, o->obj, DBUS_INTROSPECT_IFACE, "Introspect");
	auto ret = di_dbus_send(o->c, msg);
	dbus_message_unref(msg);
	return ret;
}

static void _dbus_lookup_member_cb(char *method, bool is_signal,
                                   di_object *cb, void *msg) {
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
					                 (di_object *)NULL);
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
                                bool is_signal, di_object *closure) {
	auto p = _dbus_introspect(o);

	auto cl = di_make_closure(_dbus_lookup_member_cb,
	                     (method, is_signal, closure), void *);
	di_listen_to_once(p, "reply", (void *)cl, true);
	di_unref_object((void *)cl);
	di_unref_object(p);
}
#endif

static void ioev_callback(di_object *conn, void *ptr, int event) {
	di_dbus_connection *dc = (void *)conn;
	if (event == 0) {
		dbus_watch_handle(ptr, DBUS_WATCH_READABLE);
		while (dbus_connection_dispatch(dc->conn) != DBUS_DISPATCH_COMPLETE) {}
	}
	if (event == 1) {
		dbus_watch_handle(ptr, DBUS_WATCH_WRITABLE);
	}
}

static void dbus_add_signal_handler_for(di_object *ioev, DBusWatch *w, di_dbus_connection *oc,
                                        const char *signal, int event) {
	scoped_di_object *handler =
	    (void *)di_make_closure(ioev_callback, ((di_object *)oc, (void *)w, event));
	auto l = di_listen_to(ioev, di_string_borrow(signal), handler, NULL);
	DI_CHECK_OK(di_call(l, "auto_stop", true));

	scopedp(char) * listen_handle_name;
	asprintf(&listen_handle_name, "__dbus_ioev_%s_listen_handle_for_watch_%p", signal, w);
	DI_CHECK_OK(di_member(oc, listen_handle_name, l));
}

static bool dbus_toggle_watch_impl(DBusWatch *w, void *ud, bool enabled) {
	di_dbus_connection *oc = ud;
	unsigned int flags = dbus_watch_get_flags(w);
	if (!enabled) {
		// Remove the listen handles: they are auto stop handles so this is enough
		if (flags & DBUS_WATCH_READABLE) {
			scoped_di_string listen_handle_name =
			    di_string_printf("__dbus_ioev_read_listen_handle_for_watch_%p", w);
			di_delete_member_raw((void *)oc, listen_handle_name);
		}
		if (flags & DBUS_WATCH_WRITABLE) {
			scoped_di_string listen_handle_name =
			    di_string_printf("__dbus_ioev_write_listen_handle_for_"
			                     "watch_%p",
			                     w);
			di_delete_member_raw((void *)oc, listen_handle_name);
		}
	} else {
		// Add signal listeners, this automatically starts the fdevent.
		di_object *eventm = NULL;
		di_object *di = di_object_borrow_deai((di_object *)oc);
		scoped_di_object *ioev = NULL;
		if (di_rawget_borrowed(di, "event", eventm) != 0) {
			return false;
		}
		if (di_callr(eventm, "fdevent", ioev, dbus_watch_get_unix_fd(w)) != 0) {
			return false;
		}
		if (flags & DBUS_WATCH_READABLE) {
			dbus_add_signal_handler_for(ioev, w, oc, "read", 0);
		}
		if (flags & DBUS_WATCH_WRITABLE) {
			dbus_add_signal_handler_for(ioev, w, oc, "write", 1);
		}
	}
	return true;
}

static void dbus_toggle_watch(DBusWatch *w, void *ud) {
	dbus_toggle_watch_impl(w, ud, dbus_watch_get_enabled(w));
}

static unsigned int dbus_add_watch(DBusWatch *w, void *ud) {
	di_dbus_connection *oc = ud;
	/*fprintf(stderr, "w %p, flags: %d, fd: %d\n", w, flags, fd);*/
	scoped_di_string ioev_name = di_string_printf("__dbus_ioev_for_watch_%p", w);

	if (di_lookup((void *)oc, ioev_name) != 0) {
		// We are already watching this fd?
		DI_PANIC("Same watch added multiple times by dbus");
	}
	if (dbus_watch_get_enabled(w)) {
		dbus_toggle_watch_impl(w, ud, true);
	}
	return true;
}

static void dbus_remove_watch(DBusWatch *w, void *ud) {
	di_object *conn = ud;
	DI_CHECK(conn != NULL);

	// Stop signal listeners
	dbus_toggle_watch_impl(w, ud, false);
}

static DBusHandlerResult dbus_filter(DBusConnection *conn, DBusMessage *msg, void *ud);

static inline void di_dbus_nsignal_inc(di_dbus_connection *c) {
	c->nsignals += 1;
	if (c->nsignals == 1) {
		dbus_connection_set_watch_functions(c->conn, dbus_add_watch, dbus_remove_watch,
		                                    dbus_toggle_watch, c, NULL);
		dbus_connection_add_filter(c->conn, dbus_filter, c, NULL);
	}
}

static inline void di_dbus_nsignal_dec(di_dbus_connection *c) {
	c->nsignals -= 1;
	if (c->nsignals == 0) {
		// Clear the watch functions so they won't get called. This should also
		// remove the watches so we would free the ioev object.
		dbus_connection_set_watch_functions(c->conn, NULL, NULL, NULL, NULL, NULL);
		dbus_connection_remove_filter(c->conn, dbus_filter, c);
	}
}

static inline di_object *
di_dbus_add_promise_for(di_object *conn, di_object *eventm, int64_t serial) {
	auto promise = di_new_promise(eventm);
	scoped_di_string promise_name = di_string_printf("promise_for_request_%ld", serial);
	di_add_member_clonev(conn, promise_name, DI_TYPE_OBJECT, promise);
	di_dbus_nsignal_inc((void *)conn);
	return promise;
}
static int64_t di_dbus_send_message(di_object *o, di_string type, di_string bus_,
                                    di_string objpath_, di_string iface_,
                                    di_string method_, di_string signature, di_tuple args);

/// SIGNAL: deai.plugin.dbus:DBusPendingReply.reply(,...) reply received
///
/// SIGNAL: deai.plugin.dbus:DBusPendingReply.error(,...) error received
static di_object *dbus_call_method(di_dbus_object *dobj, di_string iface,
                                   di_string method, di_string signature, di_tuple t) {

	di_object *conn = NULL;
	if (di_rawget_borrowed(dobj, "___deai_dbus_connection", conn) != 0) {
		di_throw(di_new_error("DBus connection gone"));
	}

	di_borrowm(di_object_borrow_deai(conn), event, di_throw(di_new_error("")));

	int64_t serial = -1;
	scoped_di_string bus = DI_STRING_INIT, path = DI_STRING_INIT;
	DI_CHECK_OK(di_get(dobj, "___bus_name", bus));
	DI_CHECK_OK(di_get(dobj, "___object_path", path));
	serial = di_dbus_send_message(conn, di_string_borrow_literal("method"), bus, path,
	                              iface, method, signature, t);
	if (serial < 0) {
		di_throw(di_new_error("Failed to send %d", serial));
	}

	return di_dbus_add_promise_for(conn, eventm, serial);
}

/// TYPE: deai.plugin.dbus:DBusMethod
///
/// Represents a dbus method that can be called. This object is callable.
typedef struct {
	di_object;
	di_string method;
	di_string interface;
} di_dbus_method;

static void di_dbus_free_method(di_object *o) {
	auto dbus_method = (di_dbus_method *)o;
	di_free_string(dbus_method->method);
	di_free_string(dbus_method->interface);
}

static int call_dbus_method(di_object *m, di_type *rt, di_value *ret, di_tuple t) {
	// The first argument is the dbus object
	if (t.length < 1 || t.elements[0].type != DI_TYPE_OBJECT) {
		return -EINVAL;
	}

	auto dobj = (di_dbus_object *)t.elements[0].value->object;
	auto dbus_method = (di_dbus_method *)m;
	*rt = DI_TYPE_OBJECT;

	// Skip the first object argument
	t.elements += 1;
	t.length -= 1;
	ret->object = dbus_call_method(dobj, dbus_method->interface, dbus_method->method,
	                               DI_STRING_INIT, t);
	return 0;
}

/// Call the dbus method with a signature
///
/// EXPORT: deai.plugin.dbus:DBusMethod.call_with_signature(dbus_object, signature, ...):
/// deai.plugin.dbus:DBusPendingReply
///
/// Arguments:
///
/// - dbus_object(deai.plugin.dbus:DBusObject) the dbus object that provided this method.
/// - signature(:string) a dbus type signature
///
/// Since there is multiple possible ways to serialize a deai value to dbus, sometimes
/// deai can get it wrong and not create the desired dbus types. This method accepts an
/// explicit dbus type signature and tries to match that.
static int
call_dbus_method_with_signature(di_object *m, di_type *rt, di_value *ret, di_tuple t) {
	*rt = DI_TYPE_OBJECT;
	if (t.length < 3 || t.elements[0].type != DI_TYPE_OBJECT ||
	    t.elements[1].type != DI_TYPE_OBJECT ||
	    (t.elements[2].type != DI_TYPE_STRING && t.elements[2].type != DI_TYPE_STRING_LITERAL)) {
		return -EINVAL;
	}
	auto dbus_method = (di_dbus_method *)t.elements[0].value->object;
	auto dobj = (di_dbus_object *)t.elements[1].value->object;
	di_string signature = DI_STRING_INIT;
	if (t.elements[2].type == DI_TYPE_STRING_LITERAL) {
		signature = di_string_borrow(t.elements[2].value->string_literal);
	} else {
		signature = t.elements[2].value->string;
	}
	t.elements += 3;
	t.length -= 3;
	ret->object =
	    dbus_call_method(dobj, dbus_method->interface, dbus_method->method, signature, t);
	return 0;
}

static struct di_variant di_dbus_object_getter(di_dbus_object *dobj, di_string method) {
	// Trying to get a signal object, forward to the connection object instead
	if (di_string_starts_with(method, "__signal_")) {
		struct di_variant ret = DI_VARIANT_INIT;
		ret.value = tmalloc(di_value, 1);
		if (di_rawgetx((void *)dobj, method, &ret.type, ret.value) != 0) {
			free(ret.value);
			return (struct di_variant){.type = DI_LAST_TYPE, .value = NULL};
		}
		return ret;
	}

	di_object *ret = (void *)di_new_object_with_type(di_dbus_method);
	di_set_type(ret, "deai.plugin.dbus:DBusMethod");

	di_dbus_method *mo = (void *)ret;
	mo->method = di_clone_string(method);
	DI_CHECK_OK(di_get(dobj, "___interface", mo->interface));

	di_set_object_dtor((void *)ret, di_dbus_free_method);
	di_set_object_call((void *)ret, call_dbus_method);

	auto cwm = di_new_object_with_type(di_object);
	di_set_object_call(cwm, call_dbus_method_with_signature);
	di_member(ret, "call_with_signature", cwm);

	di_value *value = tmalloc(di_value, 1);
	value->object = ret;
	return (struct di_variant){.type = DI_TYPE_OBJECT, .value = value};
}

static char *to_dbus_match_rule(di_string path, di_string interface, di_string signal) {
	char *match;
	if (interface.length != 0) {
		asprintf(&match,
		         "type='signal',path='%.*s',interface='%.*s'"
		         ",member='%.*s'",
		         (int)path.length, path.data, (int)interface.length, interface.data,
		         (int)signal.length, signal.data);
	} else {
		asprintf(&match, "type='signal',path='%.*s',member='%.*s'", (int)path.length,
		         path.data, (int)signal.length, signal.data);
	}
	return match;
}

static void
di_dbus_object_new_signal(di_dbus_object *dobj, di_string member_name, di_object *sig) {
	if (!di_string_starts_with(member_name, "__signal_")) {
		// Ignore this member
		return;
	}

	auto signal_name = di_suffix(member_name, strlen("__signal_"));
	if (!signal_name.data || !signal_name.length) {
		return;
	}

	scoped_di_string path = DI_STRING_INIT;
	DI_CHECK_OK(di_get(dobj, "___object_path", path));
	di_string interface = DI_STRING_INIT;
	DI_CHECK_OK(di_get(dobj, "___interface", interface));

	di_object *conn = NULL;
	if (di_rawget_borrowed(dobj, "___deai_dbus_connection", conn) != 0) {
		return;
	}

	di_dbus_connection *c = (void *)conn;
	if (!c->conn) {
		return;
	}

	char *match = to_dbus_match_rule(path, interface, signal_name);
	dbus_bus_add_match(c->conn, match, NULL);
	free(match);

	di_add_member_clonev((void *)dobj, member_name, DI_TYPE_OBJECT, sig);
	di_dbus_nsignal_inc(c);

	// Keep this object alive as long as there is a signal listener, by storing a
	// strong reference in the connection object. The connection object will
	// be kept alive by the listener to the ioev object.
	scoped_di_string keep_alive_name = di_string_printf(
	    "___keep_alive_%p_%.*s", dobj, (int)signal_name.length, signal_name.data);
	DI_CHECK_OK(di_add_member_clonev((void *)conn, keep_alive_name, DI_TYPE_OBJECT, dobj));
}

static void di_dbus_object_del_signal(di_dbus_object *obj, di_string member_name) {
	if (!di_string_starts_with(member_name, "__signal_")) {
		// Ignore this member
		return;
	}

	if (di_delete_member_raw((void *)obj, member_name) != 0) {
		return;
	}

	scoped_di_object *conn = NULL;
	if (di_get(obj, "___deai_dbus_connection", conn) != 0) {
		return;
	}

	di_dbus_connection *c = (void *)conn;
	if (!c->conn) {
		return;
	}

	auto signal_name = di_suffix(member_name, strlen("__signal_"));
	if (!signal_name.data || !signal_name.length) {
		return;
	}

	scoped_di_string path = DI_STRING_INIT;
	DI_CHECK_OK(di_get(obj, "___object_path", path));
	di_string interface = DI_STRING_INIT;
	di_string member = DI_STRING_INIT;
	if (!di_string_split_once(signal_name, '.', &interface, &member)) {
		member = signal_name;
	}

	auto match = to_dbus_match_rule(path, interface, member);
	dbus_bus_remove_match(c->conn, match, NULL);
	free(match);

	di_dbus_nsignal_dec(c);
	/// Stop keeping this object alive
	scoped_di_string keep_alive_name = di_string_printf(
	    "___keep_alive_%p_%.*s", obj, (int)signal_name.length, signal_name.data);
	DI_CHECK_OK(di_delete_member_raw((void *)conn, keep_alive_name));
}

/// Send a message to dbus
///
/// EXPORT: dbus.session_bus.send(type, bus, object_path, interface, method, signature, args): :integer
///
/// Arguments:
///
/// - bus(:string) recipent of this message, not used if type is "signal"
///
/// Returns a serial number if type is 'method'.
static int64_t di_dbus_send_message(di_object *o, di_string type, di_string bus_,
                                    di_string objpath_, di_string iface_,
                                    di_string method_, di_string signature, di_tuple args) {
	DBusMessage *msg = NULL;
	scopedp(char) *bus = di_string_to_chars_alloc(bus_);
	scopedp(char) *objpath = di_string_to_chars_alloc(objpath_);
	scopedp(char) *method = di_string_to_chars_alloc(method_);
	scopedp(char) *iface = di_string_to_chars_alloc(iface_);

	if (di_string_eq(type, di_string_borrow_literal("method"))) {
		msg = dbus_message_new_method_call(bus, objpath, iface, method);
	} else if (di_string_eq(type, di_string_borrow_literal("signal"))) {
		msg = dbus_message_new_signal(objpath, iface, method);
	} else {
		return -1;
	}

	DBusMessageIter i;
	dbus_message_iter_init_append(msg, &i);
	if (dbus_serialize_struct(&i, args, signature) < 0) {
		dbus_message_unref(msg);
		return -1;
	}

	di_dbus_connection *conn = (void *)o;
	uint32_t serial;
	bool success = dbus_connection_send(conn->conn, msg, &serial);
	dbus_message_unref(msg);
	if (!success) {
		return -1;
	}
	return serial;
}

static void di_dbus_name_changed(di_object *conn, di_string well_known,
                                 di_string old_owner, di_string new_owner) {
	fprintf(stderr, "DBus name changed for %.*s: %.*s -> %.*s\n", (int)well_known.length,
	        well_known.data, (int)old_owner.length, old_owner.data, (int)new_owner.length,
	        new_owner.data);
	if (di_string_starts_with(well_known, ":")) {
		// Not a well-known name
		return;
	}
	if (old_owner.length != 0) {
		// We know the old_owner, so remove the old record of the well-known name
		scoped_di_string peer_object_name =
		    di_string_printf("peer_%.*s", (int)old_owner.length, old_owner.data);
		scoped_di_object *peer = di_get_object_via_weak(conn, peer_object_name);
		if (peer != NULL) {
			di_delete_member_raw(peer, well_known);
		}
	}
	// Get the object cache directory for the well_known name.
	scoped_di_string directory_name =
	    di_string_printf("object_cache_%.*s", (int)well_known.length, well_known.data);
	scoped_di_object *directory = di_get_object_via_weak(conn, directory_name);
	if (directory == NULL) {
		// We don't have any object cache for this well-known name, we don't care
		// about its owner.
		return;
	}

	scoped_di_string recorded_owner_name = DI_STRING_INIT;
	if (di_get(directory, "___owner", recorded_owner_name) == 0 &&
	    old_owner.length != 0 && !di_string_eq(old_owner, recorded_owner_name)) {
		// The old_owner we got is different from the recorded owner. This can happen if some
		// the signals are not delivered properly or lost. We are desynced with
		// the dbus daemon. We should clear the object and owner cache and start
		// over.
		// All existing objects will not work anymore in this case.
		di_log_va(log_module, DI_LOG_WARN,
		          "dbus: name owner desynced for %.*s: old owner %.*s, new owner "
		          "%.*s, recorded_owner: %.*s",
		          (int)well_known.length, well_known.data, (int)old_owner.length,
		          old_owner.data, (int)new_owner.length, new_owner.data,
		          (int)recorded_owner_name.length, recorded_owner_name.data);
		auto all_memebers = di_get_all_member_names_raw(conn);
		for (int i = 0; i < all_memebers.length; i++) {
			di_string curr = ((di_string *)all_memebers.arr)[i];
			if (di_string_starts_with(curr, "peer_") ||
			    di_string_starts_with(curr, "object_cache_")) {
				di_delete_member_raw(conn, curr);
			}
		}
	}
	// Update owner
	if (new_owner.length > 0) {
		DI_CHECK_OK(di_rawsetx(directory, di_string_borrow_literal("___owner_name"),
		                       DI_TYPE_STRING, (void *)&new_owner));
		scoped_di_string peer_object_name =
		    di_string_printf("peer_%.*s", (int)new_owner.length, new_owner.data);
		scoped_di_object *peer = di_get_object_via_weak(conn, peer_object_name);
		if (peer == NULL) {
			peer = di_new_object_with_type(di_object);

			scoped_di_weak_object *weak_peer = di_weakly_ref_object(peer);
			DI_CHECK_OK(di_add_member_clonev(conn, peer_object_name, DI_TYPE_WEAK_OBJECT,
			                                 weak_peer));
		}
		DI_CHECK_OK(di_rawsetx(directory, di_string_borrow_literal("___owner"),
		                       DI_TYPE_OBJECT, (void *)&peer));

		scoped_di_weak_object *weak_directory = di_weakly_ref_object(directory);
		DI_CHECK_OK(di_rawsetx(peer, well_known, DI_TYPE_WEAK_OBJECT, (void *)&weak_directory));
	} else {
		di_log_va(log_module, DI_LOG_DEBUG, "dbus: name %.*s unowned",
		          (int)well_known.length, well_known.data);
		di_delete_member_raw(directory, di_string_borrow_literal("___owner"));
		di_delete_member_raw(directory, di_string_borrow_literal("___owner_name"));
	}
}

static void di_dbus_object_set_owner(di_object *o, di_tuple data) {
	DI_CHECK(data.length == 2 && data.elements[0].type == DI_TYPE_BOOL &&
	         data.elements[1].type == DI_TYPE_TUPLE);
	auto payload = data.elements[1].value->tuple;
	if (data.elements[0].value->bool_) {
		// Is error
		if (payload.length >= 1 && payload.elements[0].type == DI_TYPE_STRING) {
			auto msg = payload.elements[0].value->string;
			di_log_va(log_module, DI_LOG_ERROR, "GetNameOwner failed %.*s",
			          (int)msg.length, msg.data);
		}
		return;
	}
	if (payload.length != 1 || payload.elements[0].type != DI_TYPE_STRING) {
		di_log_va(log_module, DI_LOG_ERROR, "GetNameOwner returned wrong type");
		return;
	}

	scoped_di_object *conn = NULL;
	DI_CHECK_OK(di_get(o, "___deai_dbus_connection", conn));

	scoped_di_object *object_cache = NULL;
	DI_CHECK_OK(di_get(o, "___object_cache", object_cache));

	scoped_di_string bus_name = DI_STRING_INIT;
	DI_CHECK_OK(di_get(object_cache, "___bus_name", bus_name));

	scoped_di_string owner = DI_STRING_INIT;
	di_get(object_cache, "___owner_name", owner);
	di_dbus_name_changed(conn, bus_name, owner, payload.elements[0].value->string);
}

static di_object *di_dbus_get_property(di_object *dobj, di_string property) {
	di_object *conn = NULL;
	if (di_rawget_borrowed(dobj, "___deai_dbus_connection", conn) != 0) {
		di_throw(di_new_error("DBus connection gone"));
	}
	di_borrowm(di_object_borrow_deai(conn), event, di_throw(di_new_error("")));
	scoped_di_string obj = DI_STRING_INIT, bus = DI_STRING_INIT, interface = DI_STRING_INIT;
	DI_CHECK_OK(di_get(dobj, "___object_path", obj));
	DI_CHECK_OK(di_get(dobj, "___bus_name", bus));
	DI_CHECK_OK(di_get(dobj, "___interface", interface));
	auto serial = di_dbus_send_message(
	    conn, di_string_borrow_literal("method"), bus, obj,
	    di_string_borrow_literal(DBUS_INTERFACE_PROPERTIES), di_string_borrow_literal("Get"),
	    di_string_borrow_literal(""), di_make_tuple(interface, property));
	if (serial < 0) {
		di_throw(di_new_error("DBus error"));
	}
	return di_dbus_add_promise_for(conn, eventm, serial);
}

static di_object *di_dbus_set_property(di_object *dobj, di_string property, di_variant value) {
	di_object *conn = NULL;
	if (di_rawget_borrowed(dobj, "___deai_dbus_connection", conn) != 0) {
		di_throw(di_new_error("DBus connection gone"));
	}
	di_borrowm(di_object_borrow_deai(conn), event, di_throw(di_new_error("")));
	scoped_di_string obj = DI_STRING_INIT, bus = DI_STRING_INIT, interface = DI_STRING_INIT;
	DI_CHECK_OK(di_get(dobj, "___object_path", obj));
	DI_CHECK_OK(di_get(dobj, "___bus_name", bus));
	DI_CHECK_OK(di_get(dobj, "___interface", interface));
	auto serial = di_dbus_send_message(
	    conn, di_string_borrow_literal("method"), bus, obj,
	    di_string_borrow_literal(DBUS_INTERFACE_PROPERTIES), di_string_borrow_literal("Set"),
	    di_string_borrow_literal("ssv"), di_make_tuple(interface, property, value));
	if (serial < 0) {
		di_throw(di_new_error("DBus error"));
	}
	return di_dbus_add_promise_for(conn, eventm, serial);
}

/// Get a DBus object
///
/// EXPORT: dbus.session_bus.get(destionation: :string, object_path: :string): deai.plugin.dbus:DBusObject
///
/// (Note: getting properties are not implemented yet, but you can call
/// org.freedesktop.DBus.Properties.Get manually)
///
/// Create a proxy object for a DBus object. Methods are reflected as members of this
/// object. DBus signals are also converted to signals emitted from this object. Methods
/// must be accessed with their fully qualified names, i.e. "interface.Method". Proxy
/// objects for methods are returned, see :lua:mod:`deai.plugin.dbus.DBusMethod`.
///
/// Calling a dbus method will not give you a reply directly, instead an object is
/// returned. Listen for a "reply" signal on the object to receive the reply.
///
/// For how DBus types map to deai type, see :lua:mod:`dbus` for more details.
static di_object *
di_dbus_get_object(di_object *o, di_string bus, di_string obj, di_string interface) {
	di_borrowm(di_object_borrow_deai(o), event, di_throw(di_new_error("")));

	scoped_di_string object_cache_name =
	    di_string_printf("object_cache_%.*s", (int)bus.length, bus.data);
	scoped_di_string obj_and_interface = di_string_printf(
	    "%.*s@%.*s", (int)obj.length, obj.data, (int)interface.length, interface.data);

	scoped_di_object *object_cache = di_get_object_via_weak(o, object_cache_name);
	if (object_cache == NULL) {
		object_cache = di_new_object_with_type(di_object);
		DI_CHECK_OK(di_add_member_clonev(
		    object_cache, di_string_borrow_literal("___bus_name"), DI_TYPE_STRING, bus));

		struct di_weak_object *weak_object_cache = di_weakly_ref_object(object_cache);
		di_add_member_move(o, object_cache_name, (di_type[]){DI_TYPE_WEAK_OBJECT},
		                   &weak_object_cache);
	}

	di_object *ret = di_get_object_via_weak(object_cache, obj_and_interface);
	if (ret != NULL) {
		return ret;
	}
	ret = di_new_object_with_type(di_object);
	di_set_type(ret, "deai.plugin.dbus:DBusObject");

	// don't need to be strong as DBusObject will add signal handlers on the
	// connection object, which should keep it alive.
	DI_CHECK_OK(di_add_member_clonev(
	    ret, di_string_borrow_literal("___deai_dbus_connection"), DI_TYPE_OBJECT, o));

	di_member_clone(ret, "___bus_name", bus);
	di_member_clone(ret, "___object_path", obj);
	di_member_clone(ret, "___interface", interface);

	di_method(ret, "__get", di_dbus_object_getter, di_string);
	di_method(ret, "__set", di_dbus_object_new_signal, di_string, di_object *);
	di_method(ret, "__delete", di_dbus_object_del_signal, di_string);
	di_method(ret, "get", di_dbus_get_property, di_string);
	di_method(ret, "set", di_dbus_set_property, di_string, di_variant);

	// Keep the cache directory object alive
	di_member_clone(ret, "___object_cache", object_cache);

	struct di_weak_object *weak_object = di_weakly_ref_object(ret);
	di_add_member_move(object_cache, obj_and_interface, (di_type[]){DI_TYPE_WEAK_OBJECT},
	                   &weak_object);

	// Do way know the owner of the well-known name yet?
	scoped_di_string owner = DI_STRING_INIT;
	if (di_get(object_cache, "___owner", owner) != 0) {
		auto serial = di_dbus_send_message(
		    o, di_string_borrow_literal("method"), di_string_borrow_literal(DBUS_SERVICE_DBUS),
		    di_string_borrow_literal(DBUS_PATH_DBUS),
		    di_string_borrow_literal(DBUS_INTERFACE_DBUS),
		    di_string_borrow_literal("GetNameOwner"), di_string_borrow_literal("s"),
		    (di_tuple){
		        .length = 1,
		        .elements =
		            &(struct di_variant){
		                .type = DI_TYPE_STRING,
		                .value = &(di_value){.string = bus},
		            },
		    });

		if (serial < 0) {
			di_unref_object((void *)ret);
			di_throw(di_new_error("Failed to send GetNameOwner request"));
		}

		{
			scoped_di_closure *set_owner =
			    di_make_closure(di_dbus_object_set_owner, ((di_object *)ret), di_tuple);
			scoped_di_object *promise = di_dbus_add_promise_for(o, eventm, serial);
			// We don't care about the promise returned by `then`
			di_unref_object(di_promise_then(promise, (void *)set_owner));
		}
	}

	return (void *)ret;
}

struct di_dbus_shutdown_handler {
	di_object;
	uint64_t root_handle;
	DBusConnection *conn;
};

static void di_dbus_shutdown_part2(di_object *self_) {
	auto self = (struct di_dbus_shutdown_handler *)self_;
	dbus_connection_close(self->conn);
	dbus_connection_unref(self->conn);
}

static int di_dbus_drop_root(di_object *self_, di_type *rtype, di_value *unused value,
                             di_tuple unused args) {
	*rtype = DI_TYPE_NIL;
	// Remove the listen handle so self gets dropped and
	// di_dbus_shutdown_part2 gets called
	di_delete_member_raw(self_, di_string_borrow_literal("___listen_handle"));
	return 0;
}

static void di_dbus_shutdown(di_object *obj) {
	auto conn = (di_dbus_connection *)obj;
	if (!conn->conn) {
		return;
	}

	if (conn->nsignals > 0) {
		// Clear the watch functions otherwise they could be called from shutdown_part2
		dbus_connection_set_watch_functions(conn->conn, NULL, NULL, NULL, NULL, NULL);
		dbus_connection_remove_filter(conn->conn, dbus_filter, conn);
	}

	di_object *di = di_object_borrow_deai((di_object *)conn);
	di_object *eventm = NULL;
	di_rawget_borrowed(di, "event", eventm);

	if (eventm != NULL) {
		// This function might be called in dbus dispatch function, closing connection in
		// that context is bad. So, we drop it in the "prepare" signal handler, to make
		// sure it is dropped when there is nothing on the stack.
		auto shutdown = di_new_object_with_type(struct di_dbus_shutdown_handler);
		shutdown->conn = conn->conn;
		di_set_object_dtor((void *)shutdown, di_dbus_shutdown_part2);
		di_set_object_call((void *)shutdown, di_dbus_drop_root);

		auto listen_handle = di_listen_to(eventm, di_string_borrow_literal("prepare"),
		                                  (di_object *)shutdown, NULL);

		DI_CHECK_OK(di_call(listen_handle, "auto_stop", true));
		DI_CHECK_OK(di_member(shutdown, "___listen_handle", listen_handle));
		di_unref_object((void *)shutdown);
	} else {
		dbus_connection_close(conn->conn);
		dbus_connection_unref(conn->conn);
	}

	conn->conn = NULL;
}

static void
di_dbus_name_changed(di_object *, di_string name, di_string old_owner, di_string new_owner);
struct di_signal_info {
	di_string path;
	di_string interface;
	di_string member;
	di_tuple args;
};
static void di_emit_signal_for(di_object *directory, struct di_signal_info *info) {
	scoped_di_string obj_with_interface =
	    di_string_printf("%.*s@%.*s", (int)info->path.length, info->path.data,
	                     (int)info->interface.length, info->interface.data);
	scoped_di_object *obj = di_get_object_via_weak(directory, obj_with_interface);
	if (obj == NULL) {
		return;
	}
	di_emitn(obj, info->member, info->args);
}

static bool di_peer_foreach_cb(di_string name, di_type type, di_value *value, void *ud) {
	struct di_signal_info *info = ud;
	if (type != DI_TYPE_WEAK_OBJECT) {
		return false;
	}
	scoped_di_object *obj = di_upgrade_weak_ref(value->weak_object);
	if (obj == NULL) {
		return false;
	}
	di_emit_signal_for(obj, info);
	return false;
}

static DBusHandlerResult dbus_filter(DBusConnection *conn, DBusMessage *msg, void *ud) {
	auto type = dbus_message_get_type(msg);
	if (type != DBUS_MESSAGE_TYPE_SIGNAL && type != DBUS_MESSAGE_TYPE_METHOD_RETURN &&
	    type != DBUS_MESSAGE_TYPE_ERROR) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	DBusMessageIter i;
	dbus_message_iter_init(msg, &i);

	scoped_di_tuple t;
	dbus_deserialize_struct(&i, &t);

	// Prevent connection object from dying during signal emission
	scoped_di_object *obj = di_ref_object(ud);
	di_dbus_connection *c = (void *)obj;

	if (type == DBUS_MESSAGE_TYPE_SIGNAL) {
		if (strcmp(dbus_message_get_member(msg), "NameOwnerChanged") == 0 &&
		    strcmp(dbus_message_get_sender(msg), DBUS_SERVICE_DBUS) == 0 &&
		    strcmp(dbus_message_get_path(msg), DBUS_PATH_DBUS) == 0 &&
		    strcmp(dbus_message_get_interface(msg), DBUS_INTERFACE_DBUS) == 0) {
			// Handle name change
			if (t.length != 3 || t.elements[0].type != DI_TYPE_STRING ||
			    t.elements[1].type != DI_TYPE_STRING || t.elements[2].type != DI_TYPE_STRING) {
				// DBus sends signals for name changes with wrong payload type?
				return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
			}
			auto name = t.elements[0].value->string;
			auto old_owner = t.elements[1].value->string;
			auto new_owner = t.elements[2].value->string;
			di_dbus_name_changed(obj, name, old_owner, new_owner);
		}

		auto bus_name = dbus_message_get_sender(msg);
		auto path = dbus_message_get_path(msg);
		auto ifc = dbus_message_get_interface(msg);
		auto mbr = dbus_message_get_member(msg);

		if (!bus_name) {
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		}

		struct di_signal_info info = {
		    .path = di_string_borrow(path),
		    .interface = di_string_borrow(ifc),
		    .member = di_string_borrow(mbr),
		    .args = t,
		};
		if (bus_name[0] == ':') {
			scoped_di_string peer_object_name = di_string_printf("peer_%s", bus_name);
			scoped_di_object *peer = di_get_object_via_weak((void *)c, peer_object_name);

			if (peer != NULL) {
				di_foreach_member_raw(peer, di_peer_foreach_cb, &info);
			}
		} else {
			scoped_di_string directory_name = di_string_printf("object_cache_%s", bus_name);
			scoped_di_object *directory = di_get_object_via_weak((void *)c, directory_name);
			if (directory) {
				di_emit_signal_for(directory, &info);
			}
		}
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	if (type == DBUS_MESSAGE_TYPE_ERROR || type == DBUS_MESSAGE_TYPE_METHOD_RETURN) {
		auto serial = dbus_message_get_reply_serial(msg);
		scoped_di_string promise_name = di_string_printf("promise_for_request_%u", serial);
		bool is_error = (type == DBUS_MESSAGE_TYPE_ERROR);
		di_object *promise = NULL;
		if (di_rawget_borrowed2(ud, promise_name, promise) == 0) {
			if (is_error) {
				auto msg = t.elements[0].value->string;
				auto err = di_new_error("%.*s", (int)msg.length, msg.data);
				di_resolve_promise((void *)promise, di_make_variant(err));
				di_unref_object(err);
			} else {
				di_variant args;
				if (t.length == 1) {
					args = t.elements[0];
				} else {
					args = di_make_variant(t);
				}
				di_resolve_promise((void *)promise, args);
			}
			di_delete_member_raw(obj, promise_name);
			di_dbus_nsignal_dec((void *)obj);
		}
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/// DBus session bus
///
/// EXPORT: dbus.session_bus: deai.plugin.dbus:DBusConnection
///
/// A connection to the DBus session bus.
static di_object *di_dbus_connection_to_di(struct di_module *m, DBusConnection *conn) {
	auto di = di_module_get_deai(m);
	if (di == NULL) {
		di_throw(di_new_error("deai is shutting down..."));
	}

	dbus_connection_set_exit_on_disconnect(conn, 0);
	auto ret = di_new_object_with_type(di_dbus_connection);
	di_set_type((di_object *)ret, "deai.plugin.dbus:DBusConnection");

	ret->conn = conn;
	// TODO(yshui): keep deai alive only when there are listeners or pending replies.
	di_member(ret, DEAI_MEMBER_NAME_RAW, di);

	di_method(ret, "send", di_dbus_send_message, di_string, di_string, di_string,
	          di_string, di_string, di_string, di_tuple);
	di_method(ret, "get", di_dbus_get_object, di_string, di_string, di_string);

	di_set_object_dtor((void *)ret, (void *)di_dbus_shutdown);

	return (void *)ret;
}

static di_object *di_dbus_get_session_bus(di_object *o) {
	DBusError e;
	dbus_error_init(&e);

	DBusConnection *conn = dbus_bus_get_private(DBUS_BUS_SESSION, &e);
	if (conn == NULL) {
		auto ret = di_new_error(e.message);
		dbus_error_free(&e);
		di_throw(ret);
	}

	const char *match =
	    "type='signal',sender='" DBUS_SERVICE_DBUS "',path='" DBUS_PATH_DBUS "'"
	    ",interface='" DBUS_INTERFACE_DBUS "',member='NameOwnerChanged'";
	dbus_bus_add_match(conn, match, NULL);

	return di_dbus_connection_to_di((void *)o, conn);
}

static di_object *di_dbus_handle_hello_reply(di_object *conn, di_string name) {
	scopedp(char) *c_name = di_string_to_chars_alloc(name);
	di_log_va(log_module, DI_LOG_DEBUG, "unique name is: %s", c_name);
	dbus_bus_set_unique_name(((di_dbus_connection *)conn)->conn, c_name);

	const char *match =
	    "type='signal',sender='" DBUS_SERVICE_DBUS "',path='" DBUS_PATH_DBUS "'"
	    ",interface='" DBUS_INTERFACE_DBUS "',member='NameOwnerChanged'";
	dbus_bus_add_match(((di_dbus_connection *)conn)->conn, match, NULL);
	return di_ref_object(conn);
}

static di_object *di_dbus_connect(di_object *o, di_string address) {
	DBusError e;
	di_mgetm(o, event, di_throw(di_new_error("deai is shutting down...")));
	dbus_error_init(&e);

	scopedp(char) *c_address = di_string_to_chars_alloc(address);
	DBusConnection *conn = dbus_connection_open_private(c_address, &e);
	if (conn == NULL) {
		auto ret = di_new_error(e.message);
		dbus_error_free(&e);
		di_throw(ret);
	}
	scoped_di_object *ret = di_dbus_connection_to_di((void *)o, conn);
	auto serial = di_dbus_send_message(
	    (void *)ret, di_string_borrow_literal("method"),
	    di_string_borrow_literal(DBUS_SERVICE_DBUS), di_string_borrow_literal(DBUS_PATH_DBUS),
	    di_string_borrow_literal(DBUS_INTERFACE_DBUS), di_string_borrow_literal("Hello"),
	    DI_STRING_INIT, DI_TUPLE_INIT);
	DI_CHECK(serial >= 0);
	scoped_di_object *promise = di_dbus_add_promise_for(ret, eventm, serial);
	scoped_di_closure *handler = di_make_closure(di_dbus_handle_hello_reply, (ret), di_string);
	return di_promise_then(promise, (void *)handler);
}

#ifdef UNITTESTS
static void di_dbus_unit_tests(di_object *unused obj) {
	auto signature = "a(io)i";
	auto ret = parse_dbus_signature(di_string_borrow(signature));
	DI_CHECK(ret.nchild == 2);
	DI_CHECK(ret.current.length == strlen(signature));
	DI_CHECK(ret.child[0].current.length == 5);
	DI_CHECK(ret.child[0].nchild == 1);
	DI_CHECK(ret.child[0].child[0].current.length == 4);
	DI_CHECK(ret.child[0].child[0].nchild == 2);
	free_dbus_signature(ret);

	signature = "(iii)";
	ret = parse_dbus_signature(di_string_borrow(signature));
	DI_CHECK(ret.nchild == 1);
	DI_CHECK(ret.child[0].nchild == 3);
	free_dbus_signature(ret);

	// Test unpacking of di_variant
	DBusMessage *msg = dbus_message_new_method_call("a.b", "/", "a.b.c", "asdf");
	di_tuple t = {
	    .length = 1,
	    .elements =
	        (struct di_variant[]){
	            {
	                .type = DI_TYPE_VARIANT,
	                .value =
	                    (di_value[]){
	                        {
	                            .variant =
	                                (struct di_variant){
	                                    .type = DI_TYPE_ARRAY,
	                                    .value =
	                                        (di_value[]){
	                                            {.array =
	                                                 (di_array){
	                                                     .elem_type = DI_TYPE_INT,
	                                                     .length = 3,
	                                                     .arr = (int64_t[]){1, 2, 3},
	                                                 }},
	                                        },
	                                },
	                        },
	                    },
	            },
	        },
	};
	DBusMessageIter it;
	dbus_message_iter_init_append(msg, &it);
	DI_CHECK(dbus_serialize_struct(&it, t, di_string_borrow_literal("ai")) == 0);
	dbus_message_unref(msg);
}
#endif

/// D-Bus
///
/// EXPORT: dbus: deai:module
///
/// **D-Bus types**
///
/// D-Bus Types are converted to/from deai types when reading a DBus property,
/// receiving a DBus signal, or when calling a DBus method.
///
/// Going from DBus to deai is straightforward, all signed integers becomes
/// integer, all unsigned integers becomes unsigned integer. Arrays become arrays.
/// Structs become array of variants. Unix FD is not supported currently.
///
/// Going from deai to DBus is harder. Because deai doesn't have multiple integer
/// types, it only uses DBus INT32 and UINT32, so sometimes calling a method could
/// fail. To have complete support, we need to call Introspect and parse the XML
/// to get the method signature. This is not yet implemented.
///
struct di_module *new_dbus_module(di_object *di) {
	auto m = di_new_module(di);
#ifdef UNITTESTS
	di_method(m, "run_unit_tests", di_dbus_unit_tests);
#endif
	di_getter(m, session_bus, di_dbus_get_session_bus);
	di_method(m, "connect", di_dbus_connect, di_string);
	return m;
}

DEAI_PLUGIN_ENTRY_POINT(di) {
	auto m = new_dbus_module(di);
	di_register_module(di, di_string_borrow("dbus"), &m);
}
