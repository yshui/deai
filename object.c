/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#define _GNU_SOURCE

#include <deai/callable.h>
#include <deai/helper.h>
#include <deai/object.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/mman.h>
#include "config.h"
#include "di_internal.h"
#include "utils.h"

const void *null_ptr = NULL;

#define gen_callx(fnname, getter)                                                   \
	int fnname(struct di_object *o, const char *name, di_type_t *rt,            \
	           void **ret, ...) {                                               \
		const void *val;                                                    \
		int rc = getter(o, name, DI_TYPE_OBJECT, &val);                     \
		if (rc != 0)                                                        \
			return rc;                                                  \
                                                                                    \
		auto m = *(struct di_object **)val;                                 \
		free((void *)val);                                                  \
                                                                                    \
		va_list ap;                                                         \
		va_start(ap, ret);                                                  \
		rc = di_call_objectv(m, rt, ret, ap);                               \
                                                                                    \
		di_unref_object(m);                                                 \
		return rc;                                                          \
	}

PUBLIC gen_callx(di_rawcallx, di_rawgetxt);
PUBLIC gen_callx(di_callx, di_getxt);

PUBLIC int di_rawcallxn(struct di_object *o, const char *name, di_type_t *rt,
                        void **ret, struct di_tuple t) {
	const void *val;
	int rc = di_rawgetxt(o, name, DI_TYPE_OBJECT, &val);
	if (rc != 0)
		return rc;

	auto m = *(struct di_object * _Nonnull *)val;
	free((void *)val);

	if (!m->call)
		return -EINVAL;

	rc = m->call(m, rt, ret, t);

	di_unref_object(m);
	return rc;
}

PUBLIC int di_setx(struct di_object *o, const char *name, di_type_t type, void *val) {
	auto mem = di_lookup(o, name);
	int rc;
	const void *val2;
	if (mem && mem->writable) {
		rc = di_type_conversion(type, val, mem->type, &val2);
		if (rc != 0)
			return rc;
		di_free_value(mem->type, mem->data);
		di_copy_value(mem->type, mem->data, val2);
		free((void *)val2);
		return 0;
	}

	if (!mem)
		rc = -ENOENT;
	else
		rc = -EPERM;

	// Internal names doesn't go through setter
	if (strncmp(name, "__", 2) == 0)
		return rc;

	char *buf;
	void *ret;
	di_type_t rtype;
	asprintf(&buf, "__set_%s", name);

	int rc2 =
	    di_rawcallxn(o, buf, &rtype, &ret,
	                 (struct di_tuple){1, (void *[]){val}, (di_type_t[]){type}});
	free(buf);
	if (rc2 == 0 && rtype != DI_TYPE_VOID) {
		// Ignore for now
		di_free_value(rtype, ret);
		free(ret);
	}
	if (rc2 != -ENOENT)
		return rc2;

	rc2 = di_rawcallxn(o, "__set", &rtype, &ret,
	                   (struct di_tuple){2, (void *[]){&name, val},
	                                     (di_type_t[]){DI_TYPE_STRING, type}});

	if (rc2 == 0 && rtype != DI_TYPE_VOID) {
		// Ignore for now
		di_free_value(rtype, ret);
		free(ret);
	}
	if (rc2 != -ENOENT)
		return rc2;
	return rc;
}

PUBLIC int
di_rawgetx(struct di_object *o, const char *name, di_type_t *type, const void **ret) {
	auto m = di_lookup(o, name);

	// nil type is treated as non-existent
	if (!m)
		return -ENOENT;

	*type = m->type;
	assert(di_sizeof_type(m->type) != 0);
	void *v = calloc(1, di_sizeof_type(m->type));
	di_copy_value(m->type, v, m->data);

	*ret = v;
	return 0;
}

PUBLIC int
di_getx(struct di_object *o, const char *name, di_type_t *type, const void **ret) {
	int rc = di_rawgetx(o, name, type, ret);
	if (rc == 0)
		return 0;

	// Internal names doesn't go through getter
	if (strncmp(name, "__", 2) == 0)
		return -ENOENT;

	char *buf;
	asprintf(&buf, "__get_%s", name);

	void *ret2 = NULL;
	rc = di_rawcallx(o, buf, type, &ret2, DI_LAST_TYPE);
	free(buf);
	if (rc == 0) {
		*ret = ret2;
		return rc;
	}

	rc = di_rawcallx(o, "__get", type, &ret2, DI_TYPE_STRING, name, DI_LAST_TYPE);
	if (rc != 0)
		return rc;

	*ret = ret2;
	return 0;
}

#define gen_tfunc(name, getter)                                                     \
	int name(struct di_object *o, const char *prop, di_type_t rtype,            \
	         const void **ret) {                                                \
		const void *ret2;                                                   \
		di_type_t rt;                                                       \
		int rc = getter(o, prop, &rt, &ret2);                               \
		if (rc != 0)                                                        \
			return rc;                                                  \
                                                                                    \
		rc = di_type_conversion(rt, ret2, rtype, ret);                      \
		free((void *)ret2);                                                 \
		if (rc != 0)                                                        \
			return rc;                                                  \
                                                                                    \
		return rc;                                                          \
	}

PUBLIC gen_tfunc(di_getxt, di_getx);
PUBLIC gen_tfunc(di_rawgetxt, di_rawgetx);

PUBLIC int di_set_type(struct di_object *o, const char *tyname) {
	return di_add_value_member(o, "__type", false, DI_TYPE_STRING_LITERAL, tyname);
}

PUBLIC const char *di_get_type(struct di_object *o) {
	const void *ret;
	int rc = di_getxt(o, "__type", DI_TYPE_STRING_LITERAL, &ret);
	if (rc != 0) {
		if (rc == -ENOENT)
			return "object";
		return ERR_PTR(rc);
	}

	const char *r = *(const char **)ret;
	free((void *)ret);
	return r;
}

PUBLIC bool di_check_type(struct di_object *o, const char *tyname) {
	const char *ot = di_get_type(o);
	if (IS_ERR_OR_NULL(ot))
		return false;

	return strcmp(ot, tyname) == 0;
}

PUBLIC struct di_object *di_new_object(size_t sz) {
	if (sz < sizeof(struct di_object))
		return NULL;

	struct di_object *obj = calloc(1, sz);
	obj->ref_count = 1;
	obj->state = DI_OBJECT_STATE_HEALTHY;
	return obj;
}

PUBLIC struct di_module *di_new_module(size_t size) {
	if (size < sizeof(struct di_module))
		return NULL;

	struct di_module_internal *pm = (void *)di_new_object(size);

	di_set_type((void *)pm, "module");

	return (void *)pm;
}

static void _di_remove_member(struct di_object *obj, struct di_member_internal *m) {
	HASH_DEL(*(struct di_member_internal **)&obj->members, m);

	di_free_value(m->type, m->data);

	if (m->own)
		free(m->data);
	free(m->name);
	free(m);
}

PUBLIC int di_remove_member(struct di_object *obj, const char *name) {
	auto m = di_lookup(obj, name);
	if (!m)
		return -ENOENT;

	_di_remove_member(obj, (void *)m);
	return 0;
}

// Must be called holding external references. i.e.
// __dtor shouldn't cause the reference count to drop to 0
static void di_destroy_object(struct di_object *obj) {
	assert(obj->state != DI_OBJECT_STATE_DEAD);

	if (obj->state == DI_OBJECT_STATE_HEALTHY) {
		obj->state = DI_OBJECT_STATE_ORPHANED;
		di_apoptosis(obj);
	}

	obj->state = DI_OBJECT_STATE_DEAD;

	if (obj->dtor)
		obj->dtor(obj);
	struct di_member_internal *m = (void *)obj->members;
	while (m) {
		auto next_m = m->hh.next;
#if 0
		if (m->type != DI_TYPE_OBJECT)
			fprintf(stderr, "removing member %s\n", m->name);
		else
			fprintf(stderr, "removing member %s(%d)\n", m->name,
			        *(struct di_object **)m->data
			            ? (*(struct di_object **)m->data)->ref_count
			            : -1);
#endif
		_di_remove_member(obj, m);
		m = next_m;
	}
}

PUBLIC void di_ref_object(struct di_object *obj) {
	obj->ref_count++;
}

PUBLIC void di_unref_object(struct di_object *obj) {
	assert(obj->ref_count > 0);
	obj->ref_count--;
	if (obj->ref_count == 0) {
		di_destroy_object(obj);
		free(obj);
	}
}

PUBLIC size_t di_min_return_size(size_t in) {
	if (in < sizeof(ffi_arg))
		return sizeof(ffi_arg);
	return in;
}

// cast a di_member to di_member_internal, when you need a lvalue
#define M(x) (*(struct di_member_internal **)&(x))

static int check_new_member(struct di_object *r, struct di_member_internal *m) {
	// member name rules:
	// "<name>" and "__get_<name>" can't exist at the same time
	// if "<name>" member is writable, then "__set_<name>" can't exist
	// internal names (starts with __) can't have getter/setter (might change)

	struct di_member_internal *om = NULL;

	if (!m->name)
		return -EINVAL;

	HASH_FIND_STR(M(r->members), m->name, om);
	if (om)
		return -EEXIST;

	if (strncmp(m->name, "__get_", 6) == 0) {
		const char *fname = m->name + 6;
		if (strncmp(fname, "__", 2) == 0)
			return -EINVAL;

		HASH_FIND_STR(M(r->members), m->name, om);
		if (om)
			return -EEXIST;
	} else if (strncmp(m->name, "__set_", 6) == 0) {
		const char *fname = m->name + 6;
		if (strncmp(fname, "__", 2) == 0)
			return -EINVAL;

		HASH_FIND_STR(M(r->members), m->name, om);
		if (om && om->writable)
			return -EEXIST;
	} else if (strncmp(m->name, "__", 2) != 0) {
		char *buf;
		asprintf(&buf, "__get_%s", m->name);

		HASH_FIND_STR(M(r->members), buf, om);
		free(buf);

		if (om)
			return -EEXIST;

		if (m->writable) {
			asprintf(&buf, "__set_%s", m->name);

			HASH_FIND_STR(M(r->members), buf, om);
			free(buf);

			if (om)
				return -EEXIST;
		}
	}
	return 0;
}

static int di_insert_member(struct di_object *r, struct di_member_internal *m) {
	int ret = check_new_member(r, (void *)m);
	if (ret != 0)
		return ret;

	HASH_ADD_KEYPTR(hh, M(r->members), m->name, strlen(m->name), m);
	return 0;
}

static int _di_add_member(struct di_object *o, const char *name, bool writable,
                          bool own, di_type_t t, void *v) {
	if (!name)
		return -EINVAL;

	auto m = tmalloc(struct di_member_internal, 1);
	m->type = t;
	m->data = v;
	m->name = strdup(name);
	m->writable = writable;
	m->own = own;

	return di_insert_member(o, m);
}

PUBLIC int di_add_value_member(struct di_object *o, const char *name, bool writable,
                               di_type_t t, ...) {
	if (di_sizeof_type(t) == 0)
		return -EINVAL;

	void *nv = calloc(1, di_sizeof_type(t));
	void *v = calloc(1, di_sizeof_type(t));
	va_list ap;

	va_start(ap, t);
	va_arg_with_di_type(ap, t, nv);
	va_end(ap);

	di_copy_value(t, v, nv);
	free(nv);

	return _di_add_member(o, name, writable, true, t, v);
}

PUBLIC int di_add_ref_member(struct di_object *o, const char *name, bool writable,
                             di_type_t t, void *addr) {
	return _di_add_member(o, name, writable, false, t, addr);
}

PUBLIC struct di_member *di_lookup(struct di_object *o, const char *name) {
	struct di_member_internal *ret = NULL;
	HASH_FIND_STR(M(o->members), name, ret);
	return (void *)ret;
}

#define chknull(v) if ((*(void **)(v)) != NULL)
PUBLIC void di_free_tuple(struct di_tuple t) {
	for (int i = 0; i < t.length; i++) {
		di_free_value(t.elem_type[i], t.tuple[i]);
		free(t.tuple[i]);
	}
	free(t.elem_type);
	free(t.tuple);
}

PUBLIC void di_free_array(struct di_array arr) {
	size_t step = di_sizeof_type(arr.elem_type);
	for (int i = 0; i < arr.length; i++)
		di_free_value(arr.elem_type, arr.arr + step * i);
	free(arr.arr);
}

PUBLIC void di_free_value(di_type_t t, void *ret) {
	switch (t) {
	case DI_TYPE_ARRAY: di_free_array(*(struct di_array *)ret); break;
	case DI_TYPE_TUPLE: di_free_tuple(*(struct di_tuple *)ret); break;
	case DI_TYPE_STRING: chknull(ret) free(*(char **)ret); break;
	case DI_TYPE_OBJECT:
		chknull(ret) di_unref_object(*(struct di_object **)ret);
		break;
	default: break;
	}
}

PUBLIC void di_copy_value(di_type_t t, void *dst, const void *src) {
	const struct di_array *arr;
	const struct di_tuple *tuple;
	struct di_tuple ret;
	void *d;

	// dst and src only allowed to be null when t is nil
	assert(t == DI_TYPE_NIL || (dst && src));
	switch (t) {
	case DI_TYPE_ARRAY:
		arr = src;
		assert(di_sizeof_type(arr->elem_type) != 0);
		d = calloc(arr->length, di_sizeof_type(arr->elem_type));
		for (int i = 0; i < arr->length; i++)
			di_copy_value(arr->elem_type,
			              d + di_sizeof_type(arr->elem_type) * i,
			              arr->arr + di_sizeof_type(arr->elem_type) * i);
		*(struct di_array *)dst =
		    (struct di_array){arr->length, d, arr->elem_type};
		break;
	case DI_TYPE_TUPLE:
		tuple = src;
		ret.tuple = tmalloc(void *, tuple->length);
		ret.elem_type = tmalloc(di_type_t, tuple->length);
		ret.length = tuple->length;
		for (int i = 0; i < tuple->length; i++) {
			ret.tuple[i] = calloc(1, di_sizeof_type(tuple->elem_type[i]));
			di_copy_value(tuple->elem_type[i], ret.tuple[i],
			              tuple->tuple[i]);
		}
		*(struct di_tuple *)dst = ret;
		break;
	case DI_TYPE_STRING:
		*(const char **)dst = strdup(*(const char **)src);
		break;
	case DI_TYPE_OBJECT:
		di_ref_object(*(struct di_object **)src);
		*(struct di_object **)dst = *(struct di_object **)src;
		break;
	case DI_TYPE_NIL:
		// nothing to do
		break;
	default: memmove(dst, src, di_sizeof_type(t)); break;
	}
}

struct di_signal {
	char *name;
	int nlisteners;
	struct di_object *owner;
	struct list_head listeners;
	UT_hash_handle hh;
};

struct di_listener {
	struct di_object;

	struct di_object *handler;
	struct di_signal *signal;

	struct list_head siblings;
	bool once;
};

static struct di_object *di_get_owner_of_listener(struct di_listener *l) {
	di_ref_object(l->signal->owner);
	return l->signal->owner;
}

static struct di_listener *di_new_listener(void) {
	struct di_listener *l = di_new_object_with_type(struct di_listener);

	// stop_listener should be set as dtor, because if a listener is
	// alive, it's ref_count is guaranteed to be > 0
	di_method(l, "stop", di_stop_listener);
	di_getter(l, owner, di_get_owner_of_listener);

	ABRT_IF_ERR(di_set_type((void *)l, "listener"));
	return l;
}

// __destroyed is a special signal, it doesn't hold reference to the object
// (otherwise the object won't be freed, catch 22), and __new_/__del_ methods
// are not called for it
#define is_destroy(n) (strcmp(n, "__destroyed") == 0)

PUBLIC struct di_listener *di_listen_to_once(struct di_object *o, const char *name,
                                             struct di_object *h, bool once) {
	if (o->state != DI_OBJECT_STATE_HEALTHY)
		return NULL;

	struct di_signal *sig = NULL;
	HASH_FIND_STR(o->signals, name, sig);
	if (!sig) {
		sig = tmalloc(struct di_signal, 1);
		sig->name = strdup(name);
		sig->owner = o;

		INIT_LIST_HEAD(&sig->listeners);
		HASH_ADD_KEYPTR(hh, o->signals, sig->name, strlen(sig->name), sig);
		if (!is_destroy(name)) {
			di_call(o, "__new_signal", name);
			di_ref_object((void *)o);
		}
	}

	auto l = di_new_listener();
	l->handler = h;
	l->signal = sig;
	l->once = once;

	di_ref_object(h);
	di_ref_object((void *)l);
	list_add(&l->siblings, &sig->listeners);

	sig->nlisteners++;

	return l;
}

PUBLIC struct di_listener *
di_listen_to(struct di_object *o, const char *name, struct di_object *h) {
	return di_listen_to_once(o, name, h, false);
}

/**
 * Remove all listeners from an object.
 * @param o the object.
 */
PUBLIC void di_clear_listeners(struct di_object *o) {
	struct di_signal *sig, *tsig;
	HASH_ITER (hh, o->signals, sig, tsig) {
		if (!is_destroy(sig->name))
			di_call(o, "__del_signal", sig->name);

		struct di_listener *l, *nl;
		list_for_each_entry_safe (l, nl, &sig->listeners, siblings) {
			list_del(&l->siblings);
			l->signal = NULL;
			di_unref_object(l->handler);
			di_call(l, "__detach");
			di_unref_object((void *)l);
		}

		HASH_DEL(o->signals, sig);
		if (!is_destroy(sig->name))
			di_unref_object(o);
		free(sig->name);
		free(sig);
	}
}

PUBLIC int di_stop_listener(struct di_listener *l) {
	if (!l->signal)
		return -ENOENT;

	list_del(&l->siblings);
	l->signal->nlisteners--;
	if (list_empty(&l->signal->listeners)) {
		HASH_DEL(l->signal->owner->signals, l->signal);
		if (!is_destroy(l->signal->name)) {
			di_call(l->signal->owner, "__del_signal", l->signal->name);
			di_unref_object((void *)l->signal->owner);
		}
		free(l->signal->name);
		free(l->signal);
	}

	di_unref_object(l->handler);
	l->signal = NULL;
	l->handler = NULL;
	di_call(l, "__detach");
	di_unref_object((void *)l);
	return 0;
}

PUBLIC int di_emitn(struct di_object *o, const char *name, struct di_tuple t) {
	assert(!(o->state & DI_OBJECT_STATE_DEAD) || !strcmp(name, "__destroyed"));
	if (o->state != DI_OBJECT_STATE_HEALTHY)
		return 0;

	if (t.length > MAX_NARGS)
		return -E2BIG;

	assert(t.length == 0 || (t.elem_type != NULL && t.tuple != NULL));

	struct di_signal *sig;
	HASH_FIND_STR(o->signals, name, sig);
	if (!sig)
		return 0;

	int cnt = 0;
	struct di_listener *l,
	    **all_l = tmalloc(struct di_listener *, sig->nlisteners);

	// Allow listeners to be removed during emission
	// One usecase: There're two object, A, B, where A -> B.
	// Both A and B listen to a signal. In A's handler, A unref B,
	// causing it to be freed. B's dtor could then remove the listener
	list_for_each_entry (l, &sig->listeners, siblings) {
		all_l[cnt++] = l;
		di_ref_object((void *)l);
	}

	assert(cnt == sig->nlisteners);

	// Prevent di_destroy_object from being called in the middle of an emission.
	// e.g. One handler might remove all listeners, causing ref_count to drop to
	// 0.
	di_ref_object(o);
	for (int i = 0; i < cnt; i++) {
		__label__ next;
		l = all_l[i];
		if (!l->handler)
			// Listener stopped
			goto next;
		di_type_t rtype;
		void *ret = NULL;
		int rc = l->handler->call(l->handler, &rtype, &ret, t);

		if (rc == 0) {
			di_free_value(rtype, ret);
			free(ret);
		} else
			fprintf(stderr, "Failed to call a listener callback: %s\n",
			        strerror(-rc));
	next:
		if (l->once)
			di_stop_listener(l);
		di_unref_object((void *)l);
	}
	free(all_l);
	di_unref_object(o);
	return 0;
}

PUBLIC void di_apoptosis(struct di_object *obj) {
	if (obj->state == DI_OBJECT_STATE_APOPTOSIS ||
	    obj->state == DI_OBJECT_STATE_DEAD)
		return;

	if (obj->state == DI_OBJECT_STATE_HEALTHY) {
		obj->state = DI_OBJECT_STATE_APOPTOSIS;
		// Prevent object death before signal emission finishes. Object
		// death can cause the death of its referees, and some of the
		// handlers
		// might need those referees to be alive.
		di_ref_object(obj);
	} /* else */
	  // apoptosis called by di_destroy_object, obj->state
	  // is already set approperiately.

	di_emitn(obj, "__destroyed", DI_TUPLE_NIL);
	di_clear_listeners(obj);
	if (obj->state == DI_OBJECT_STATE_APOPTOSIS)
		di_unref_object(obj);
}

#undef is_destroy
