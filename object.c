/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai/callable.h>
#include <deai/helper.h>
#include <deai/object.h>
#include <assert.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/mman.h>
#include "config.h"
#include "di_internal.h"
#include "utils.h"

static const struct di_object_internal dead_weakly_referenced_object = {
    .ref_count = 0,
    .weak_ref_count = 1,        // Keep this object from being freed
};

const struct di_weak_object *const dead_weak_ref =
    (const struct di_weak_object *)&dead_weakly_referenced_object;

const void *null_ptr = NULL;
// clang-format off
static_assert(sizeof(struct di_object) == sizeof(struct di_object_internal),
              "di_object size mismatch");
static_assert(alignof(struct di_object) == alignof(struct di_object_internal),
              "di_object alignment mismatch");
// clang-format on

static int di_call_internal(struct di_object *self, struct di_object *method_, di_type_t *rt,
                            union di_value *ret, struct di_tuple args, bool *called) {
	auto method = (struct di_object_internal *)method_;
	*called = false;
	if (!method->call) {
		return -EINVAL;
	}

	struct di_tuple real_args;
	real_args.length = args.length + 1;
	assert(real_args.length <= MAX_NARGS);

	/* Push the object itself as the first argument */
	real_args.elements = alloca(sizeof(struct di_variant) * real_args.length);
	memcpy(&real_args.elements[1], args.elements, sizeof(struct di_variant) * args.length);
	real_args.elements[0].type = DI_TYPE_OBJECT;
	real_args.elements[0].value = (union di_value *)&self;

	int rc = method->call(method_, rt, ret, real_args);
	*called = true;

	di_unref_object(method_);
	return rc;
}

#define gen_callx(fnname, getter)                                                        \
	int fnname(struct di_object *self, const char *name, di_type_t *rt,              \
	           union di_value *ret, struct di_tuple args, bool *called) {            \
		struct di_object *val;                                                   \
		*called = false;                                                         \
		int rc = getter(self, name, DI_TYPE_OBJECT, (union di_value *)&val);     \
		if (rc != 0) {                                                           \
			return rc;                                                       \
		}                                                                        \
		return di_call_internal(self, val, rt, ret, args, called);               \
	}

PUBLIC_DEAI_API gen_callx(di_callx, di_getxt);
PUBLIC_DEAI_API gen_callx(di_rawcallxn, di_rawgetxt);

/// Call "<prefix>_<name>" with "<prefix>" as fallback
///
/// @param[out] found whether a handler is found
static int call_handler_with_fallback(struct di_object *nonnull o,
                                      const char *nonnull prefix, const char *nonnull name,
                                      struct di_variant arg, di_type_t *nullable rtype,
                                      union di_value *nullable ret, bool *found) {
	*found = false;
	// Internal names doesn't go through handler
	if (strncmp(name, "__", 2) == 0) {
		return -ENOENT;
	}

	char *buf;
	asprintf(&buf, "%s_%s", prefix, name);
	di_type_t rtype2;
	union di_value ret2;

	struct di_variant args[2] = {arg, DI_VARIANT_INIT};
	struct di_tuple tmp = {
	    // There is a trick here.
	    // DI_LAST_TYPE is used to signify that the argument "doesn't exist". Unlike
	    // DI_TYPE_NIL, which would mean there is one argument, whose type is nil.
	    // This is not a convention. Generally speaking, DI_LAST_TYPE should never
	    // appear in argument lists.
	    .length = arg.type != DI_LAST_TYPE ? 1 : 0,
	    .elements = args,
	};
	int rc = di_rawcallxn(o, buf, &rtype2, &ret2, tmp, found);
	free(buf);

	if (*found) {
		goto ret;
	}

	tmp.length++;
	if (tmp.length > 1) {
		args[1] = args[0];
	}
	args[0] = (struct di_variant){
	    .type = DI_TYPE_STRING_LITERAL,
	    .value = &(union di_value){.string_literal = name},
	};

	rc = di_rawcallxn(o, prefix, &rtype2, &ret2, tmp, found);
ret:
	if (rc == 0) {
		if (ret && rtype) {
			*rtype = rtype2;
			*ret = ret2;
		} else {
			di_free_value(rtype2, &ret2);
		}
	}
	return rc;
}

int di_setx(struct di_object *o, const char *prop, di_type_t type, void *val) {
	bool handler_found;
	int rc2 = call_handler_with_fallback(
	    o, "__set", prop, (struct di_variant){val, type}, NULL, NULL, &handler_found);
	if (handler_found) {
		return rc2;
	}

	auto mem = di_lookup(o, prop);
	int rc;
	union di_value val2;
	bool cloned = false;
	if (mem) {
		// TODO(yshui) remove the type conversion.
		// If automatic type conversion is desired, you should use a setter
		rc = di_type_conversion(type, val, mem->type, &val2, &cloned);
		if (rc != 0) {
			return rc;
		}
		di_free_value(mem->type, mem->data);
		// No need to reallocate mem->data here since the type didn't change
		di_copy_value(mem->type, mem->data, &val2);
		if (cloned) {
			di_free_value(mem->type, &val2);
		}
		return 0;
	}

	if (!mem) {
		rc = -ENOENT;
	} else {
		rc = -EPERM;
	}
	return rc;
}

int di_rawgetx(struct di_object *o, const char *prop, di_type_t *type, union di_value *ret) {
	auto m = di_lookup(o, prop);

	// nil type is treated as non-existent
	if (!m) {
		return -ENOENT;
	}

	*type = m->type;
	assert(di_sizeof_type(m->type) != 0);
	di_copy_value(m->type, ret, m->data);
	return 0;
}

// Recusively unpack a variant until it only contains something that's not a variant
static void di_flatten_variant(struct di_variant *var) {
	// `var` might be overwritten by changing `ret`, so keep a copy first
	if (var->type == DI_TYPE_VARIANT) {
		assert(&var->value->variant != var);
		di_flatten_variant(&var->value->variant);
		union di_value *inner = var->value;
		var->type = var->value->variant.type;
		var->value = var->value->variant.value;
		if (inner) {
			free(inner);
		}
	}
}

PUBLIC_DEAI_API int di_getx(struct di_object *o, const char *name, di_type_t *type, union di_value *ret) {
	int rc = di_rawgetx(o, name, type, ret);
	if (rc == 0) {
		return 0;
	}

	bool handler_found;
	rc = call_handler_with_fallback(o, "__get", name,
	                                (struct di_variant){NULL, DI_LAST_TYPE}, type,
	                                ret, &handler_found);
	if (rc != 0) {
		return rc;
	}

	if (*type == DI_TYPE_VARIANT) {
		struct di_variant *var = &ret->variant;
		di_flatten_variant(var);
		if (var->type == DI_LAST_TYPE) {
			return -ENOENT;
		}
		*type = var->type;

		if (var->value != NULL) {
			union di_value *tmp = var->value;
			memcpy(ret, var->value, di_sizeof_type(var->type));
			free(tmp);
		}
	}
	return 0;
}

#define gen_tfunc(name, getter)                                                                 \
	int name(struct di_object *o, const char *prop, di_type_t rtype, union di_value *ret) { \
		union di_value ret2;                                                            \
		di_type_t rt;                                                                   \
		int rc = getter(o, prop, &rt, &ret2);                                           \
		if (rc != 0) {                                                                  \
			return rc;                                                              \
		}                                                                               \
		bool cloned = false;                                                            \
		rc = di_type_conversion(rt, &ret2, rtype, ret, &cloned);                        \
		if (cloned) {                                                                   \
			di_free_value(rt, &ret2);                                               \
		}                                                                               \
		return rc;                                                                      \
	}

gen_tfunc(di_getxt, di_getx);
gen_tfunc(di_rawgetxt, di_rawgetx);

int di_set_type(struct di_object *o, const char *type) {
	return di_add_member_clone(o, "__type", DI_TYPE_STRING_LITERAL, type);
}

const char *di_get_type(struct di_object *o) {
	const char *ret;
	int rc = di_rawgetxt(o, "__type", DI_TYPE_STRING_LITERAL, (union di_value *)&ret);
	if (rc != 0) {
		if (rc == -ENOENT) {
			return "deai:object";
		}
		return ERR_PTR(rc);
	}

	return ret;
}

bool di_check_type(struct di_object *o, const char *tyname) {
	const char *ot = di_get_type(o);
	if (IS_ERR_OR_NULL(ot)) {
		return false;
	}

	return strcmp(ot, tyname) == 0;
}

#ifdef TRACK_OBJECTS
thread_local struct list_head all_objects;
#endif

struct di_object *di_new_object(size_t sz, size_t alignment) {
	if (sz < sizeof(struct di_object)) {
		return NULL;
	}
	if (alignment < alignof(struct di_object)) {
		return NULL;
	}

	struct di_object_internal *obj;
	posix_memalign((void **)&obj, alignment, sz);
	memset(obj, 0, sz);
	obj->ref_count = 1;

	// non-zero strong references will implicitly hold a weak refrence. that reference
	// is only dropped when the object destruction finishes. this is to avoid the
	// case where the last weak reference to the object is dropped during object
	// destruction, thus cause the object to be freed in the middle of destruction.
	obj->weak_ref_count = 1;
	obj->destroyed = 0;

#ifdef TRACK_OBJECTS
	list_add(&obj->siblings, &all_objects);
#endif

	return (struct di_object *)obj;
}

struct di_module *di_new_module_with_size(struct deai *di, size_t size) {
	if (size < sizeof(struct di_module)) {
		return NULL;
	}

	struct di_module *pm = (void *)di_new_object(size, alignof(max_align_t));

	di_set_type((void *)pm, "deai:module");
	pm->di = di;

	return (void *)pm;
}

PUBLIC_DEAI_API struct di_module *di_new_module(struct deai *di) {
	return di_new_module_with_size(di, sizeof(struct di_module));
}

static void _di_remove_member_raw(struct di_object_internal *obj, struct di_member *m) {
	HASH_DEL(*(struct di_member **)&obj->members, m);

	di_free_value(m->type, m->data);
	free(m->data);
	free(m->name);
	free(m);
}

PUBLIC_DEAI_API int di_remove_member_raw(struct di_object *obj, const char *name) {
	auto m = di_lookup(obj, name);
	if (!m) {
		return -ENOENT;
	}

	_di_remove_member_raw((struct di_object_internal *)obj, (void *)m);
	return 0;
}

PUBLIC_DEAI_API int di_remove_member(struct di_object *obj, const char *name) {
	bool handler_found;
	int rc2 = call_handler_with_fallback(obj, "__delete", name,
	                                     (struct di_variant){NULL, DI_LAST_TYPE},
	                                     NULL, NULL, &handler_found);
	if (handler_found) {
		return rc2;
	}

	return di_remove_member_raw(obj, name);
}

// Try to never call destroy twice on something. Although it's fine to do so
PUBLIC_DEAI_API void di_destroy_object(struct di_object *_obj) {
	auto obj = (struct di_object_internal *)_obj;

	// Prevent destroy from being called while we are destroying
	di_ref_object(_obj);
	if (obj->destroyed) {
		fprintf(stderr, "warning: destroy object multiple times\n");
	}
	obj->destroyed = 1;

	// Call dtor before removing members to allow the dtor to check __type
	// Never call dtor more than once
	if (obj->dtor) {
		auto tmp = obj->dtor;
		obj->dtor = NULL;
		tmp(_obj);
	}

	struct di_member *m = (void *)obj->members;
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
		_di_remove_member_raw(obj, m);
		m = next_m;
	}

	di_unref_object(_obj);
}

PUBLIC_DEAI_API struct di_object *di_ref_object(struct di_object *_obj) {
	auto obj = (struct di_object_internal *)_obj;
	obj->ref_count++;
	return _obj;
}

struct di_weak_object *di_weakly_ref_object(struct di_object *_obj) {
	auto obj = (struct di_object_internal *)_obj;
	obj->weak_ref_count++;
	return (struct di_weak_object *)obj;
}

struct di_object *nullable di_upgrade_weak_ref(struct di_weak_object *weak) {
	assert(weak != PTR_POISON);
	auto obj = (struct di_object_internal *)weak;
	if (obj->ref_count > 0) {
		return di_ref_object((struct di_object *)obj);
	}
	return NULL;
}

static inline void di_decrement_weak_ref_count(struct di_object_internal *obj) {
	obj->weak_ref_count--;
	if (obj->weak_ref_count == 0) {
#ifdef TRACK_OBJECTS
		list_del(&obj->siblings);
#endif
		free(obj);
	}
}

void di_drop_weak_ref(struct di_weak_object **weak) {
	assert(*weak != PTR_POISON);
	auto obj = (struct di_object_internal *)*weak;
	di_decrement_weak_ref_count(obj);
	*weak = PTR_POISON;
}

void di_unref_object(struct di_object *_obj) {
	auto obj = (struct di_object_internal *)_obj;
	assert(obj->ref_count > 0);
	obj->ref_count--;
	if (obj->ref_count == 0) {
		if (obj->destroyed) {
			// If we reach here, destroy must have completed
			di_decrement_weak_ref_count(obj);
		} else {
			di_destroy_object(_obj);
		}
	}
}

size_t di_min_return_size(size_t in) {
	if (in < sizeof(ffi_arg)) {
		return sizeof(ffi_arg);
	}
	return in;
}

static int check_new_member(struct di_object_internal *obj, struct di_member *m) {
	// member name rules:
	// internal names (starts with __) can't have getter/setter/deleter (might change)

	struct di_member *om = NULL;

	if (!m->name) {
		return -EINVAL;
	}

	HASH_FIND_STR(obj->members, m->name, om);
	if (om) {
		return -EEXIST;
	}

	static const char *const getter_prefix = "__get_";
	const size_t getter_prefix_len = strlen(getter_prefix);
	static const char *const setter_prefix = "__set_";
	const size_t setter_prefix_len = strlen(setter_prefix);
	static const char *const deleter_prefix = "__delete_";
	const size_t deleter_prefix_len = strlen(setter_prefix);

	char *real_name = NULL;
	if (strncmp(m->name, getter_prefix, getter_prefix_len) == 0) {
		real_name = m->name + getter_prefix_len;
	} else if (strncmp(m->name, setter_prefix, setter_prefix_len) == 0) {
		real_name = m->name + setter_prefix_len;
	} else if (strncmp(m->name, deleter_prefix, deleter_prefix_len) == 0) {
		real_name = m->name + deleter_prefix_len;
	}

	if (real_name && strncmp(real_name, "__", 2) == 0) {
		return -EINVAL;
	}
	return 0;
}

static int di_insert_member(struct di_object_internal *obj, struct di_member *m) {
	int ret = check_new_member(obj, (void *)m);
	if (ret != 0) {
		return ret;
	}

	HASH_ADD_KEYPTR(hh, obj->members, m->name, strlen(m->name), m);
	return 0;
}

static int di_add_member(struct di_object_internal *o, const char *name, di_type_t t, void *v) {
	if (!name) {
		return -EINVAL;
	}

	auto m = tmalloc(struct di_member, 1);
	m->type = t;
	m->data = v;
	m->name = strdup(name);

	int ret = di_insert_member(o, m);
	if (ret != 0) {
		di_free_value(t, v);
		free(v);

		free(m->name);
		free(m);
	}
	return ret;
}

int di_add_member_clone(struct di_object *o, const char *name, di_type_t t, ...) {
	if (di_sizeof_type(t) == 0) {
		return -EINVAL;
	}

	void *nv = calloc(1, di_sizeof_type(t));
	void *v = calloc(1, di_sizeof_type(t));
	va_list ap;

	va_start(ap, t);
	va_arg_with_di_type(ap, t, nv);
	va_end(ap);

	di_copy_value(t, v, nv);
	free(nv);

	return di_add_member((struct di_object_internal *)o, name, t, v);
}

int di_add_member_move(struct di_object *o, const char *name, di_type_t *t, void *addr) {
	auto sz = di_sizeof_type(*t);
	if (sz == 0) {
		return -EINVAL;
	}

	di_type_t tt = *t;
	void *taddr = malloc(sz);
	memcpy(taddr, addr, sz);

	*t = DI_TYPE_NIL;
	memset(addr, 0, sz);

	return di_add_member((struct di_object_internal *)o, name, tt, taddr);
}

PUBLIC_DEAI_API struct di_member *di_lookup(struct di_object *_obj, const char *name) {
	auto obj = (struct di_object_internal *)_obj;
	struct di_member *ret = NULL;
	HASH_FIND_STR(obj->members, name, ret);
	return (void *)ret;
}

PUBLIC_DEAI_API void di_set_object_dtor(struct di_object *nonnull obj, di_dtor_fn_t nullable dtor) {
	auto internal = (struct di_object_internal *)obj;
	internal->dtor = dtor;
}

PUBLIC_DEAI_API void di_set_object_call(struct di_object *nonnull obj, di_call_fn_t nullable call) {
	auto internal = (struct di_object_internal *)obj;
	internal->call = call;
}

PUBLIC_DEAI_API bool di_is_object_callable(struct di_object *nonnull obj) {
	auto internal = (struct di_object_internal *)obj;
	return internal->call != NULL;
}

PUBLIC_DEAI_API void di_free_tuple(struct di_tuple t) {
	for (int i = 0; i < t.length; i++) {
		di_free_value(DI_TYPE_VARIANT, (union di_value *)&t.elements[i]);
	}
	free(t.elements);
}

PUBLIC_DEAI_API void di_free_array(struct di_array arr) {
	size_t step = di_sizeof_type(arr.elem_type);
	for (int i = 0; i < arr.length; i++) {
		di_free_value(arr.elem_type, arr.arr + step * i);
	}
	free(arr.arr);
}

PUBLIC_DEAI_API void di_free_value(di_type_t t, union di_value *ptr) {
	if (t == DI_TYPE_NIL) {
		return;
	}

	// If t != DI_TYPE_UINT, then `ptr_` cannot be NULL
	union di_value *nonnull val = ptr;
	struct di_object *nonnull obj;
	char *nonnull string;
	switch (t) {
	case DI_TYPE_ARRAY:
		di_free_array(val->array);
		break;
	case DI_TYPE_TUPLE:
		di_free_tuple(val->tuple);
		break;
	case DI_TYPE_STRING:
		string = val->string;
		free(string);
		break;
	case DI_TYPE_OBJECT:
		obj = val->object;
		di_unref_object(obj);
		break;
	case DI_TYPE_WEAK_OBJECT:
		di_drop_weak_ref(&val->weak_object);
		break;
	case DI_TYPE_VARIANT:
		di_free_value(val->variant.type, val->variant.value);
		free(val->variant.value);
		break;
	case DI_LAST_TYPE:
	case DI_TYPE_ANY:
		DI_ASSERT(false, "Trying to free value of invalid types");
		fallthrough();
	case DI_TYPE_BOOL:
	case DI_TYPE_INT:
	case DI_TYPE_UINT:
	case DI_TYPE_NINT:
	case DI_TYPE_NUINT:
	case DI_TYPE_FLOAT:
	case DI_TYPE_POINTER:
	case DI_TYPE_STRING_LITERAL:
		// Nothing to do
		break;
	case DI_TYPE_NIL:
		// Already checked
		unreachable();
	}
}

PUBLIC_DEAI_API void di_copy_value(di_type_t t, void *dst, const void *src) {
	const struct di_array *arr;
	const struct di_tuple *tuple;
	union di_value *dstval = dst;
	const union di_value *srcval = src;
	void *d;

	// dst and src only allowed to be null when t is unit
	assert(t == DI_TYPE_NIL || (dst && src));
	switch (t) {
	case DI_TYPE_ARRAY:
		arr = &srcval->array;
		assert(di_sizeof_type(arr->elem_type) != 0);
		d = calloc(arr->length, di_sizeof_type(arr->elem_type));
		for (int i = 0; i < arr->length; i++) {
			di_copy_value(arr->elem_type, d + di_sizeof_type(arr->elem_type) * i,
			              arr->arr + di_sizeof_type(arr->elem_type) * i);
		}
		dstval->array = (struct di_array){arr->length, d, arr->elem_type};
		break;
	case DI_TYPE_TUPLE:
		tuple = &srcval->tuple;
		dstval->tuple.elements = tmalloc(struct di_variant, tuple->length);
		dstval->tuple.length = tuple->length;

		for (int i = 0; i < tuple->length; i++) {
			di_copy_value(DI_TYPE_VARIANT, &dstval->tuple.elements[i],
			              &tuple->elements[i]);
		}
		break;
	case DI_TYPE_VARIANT:
		dstval->variant = (struct di_variant){
		    .type = srcval->variant.type,
		    .value = malloc(di_sizeof_type(srcval->variant.type)),
		};
		di_copy_value(srcval->variant.type, dstval->variant.value, srcval->variant.value);
		break;
	case DI_TYPE_STRING:
		dstval->string = strdup(srcval->string);
		break;
	case DI_TYPE_OBJECT:
		di_ref_object(srcval->object);
		dstval->object = srcval->object;
		break;
	case DI_TYPE_WEAK_OBJECT:
		dstval->weak_object = di_weakly_ref_object(srcval->object);
		break;
	case DI_TYPE_NIL:
		// nothing to do
		break;
	case DI_TYPE_ANY:
	case DI_LAST_TYPE:
		DI_PANIC("Trying to copy invalid types");
	case DI_TYPE_BOOL:
	case DI_TYPE_INT:
	case DI_TYPE_UINT:
	case DI_TYPE_NINT:
	case DI_TYPE_NUINT:
	case DI_TYPE_FLOAT:
	case DI_TYPE_POINTER:
	case DI_TYPE_STRING_LITERAL:
		memmove(dst, src, di_sizeof_type(t));
		break;
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

	ABRT_IF_ERR(di_set_type((void *)l, "deai:listener"));
	return l;
}

PUBLIC_DEAI_API struct di_listener *
di_listen_to_once(struct di_object *_obj, const char *name, struct di_object *h, bool once) {
	auto obj = (struct di_object_internal *)_obj;
	assert(!obj->destroyed);

	struct di_signal *sig = NULL;
	HASH_FIND_STR(obj->signals, name, sig);
	if (!sig) {
		sig = tmalloc(struct di_signal, 1);
		sig->name = strdup(name);
		sig->owner = _obj;

		INIT_LIST_HEAD(&sig->listeners);
		HASH_ADD_KEYPTR(hh, obj->signals, sig->name, strlen(sig->name), sig);
		if (strncmp(name, "__", 2) != 0) {
			bool handler_found;
			call_handler_with_fallback(_obj, "__new_signal", sig->name,
			                           (struct di_variant){NULL, DI_LAST_TYPE},
			                           NULL, NULL, &handler_found);
			di_ref_object((void *)obj);
		}
	}

	auto l = di_new_listener();
	l->handler = h;
	l->signal = sig;
	l->once = once;

	if (h) {
		di_ref_object(h);
	}
	list_add(&l->siblings, &sig->listeners);

	sig->nlisteners++;

	return l;
}

PUBLIC_DEAI_API struct di_listener *
di_listen_to(struct di_object *o, const char *name, struct di_object *h) {
	return di_listen_to_once(o, name, h, false);
}

PUBLIC_DEAI_API int di_stop_listener(struct di_listener *l) {
	// The caller announce the intention to stop this listener
	// meaning they don't want the __detach to be called anymore
	//
	// Better remove it here even though another stop operation
	// might be in progress (indicated by ->signal == NULL)
	di_remove_member_raw((struct di_object *)l, "__detach");

	if (!l->signal) {
		return -ENOENT;
	}

	list_del(&l->siblings);
	l->signal->nlisteners--;
	if (list_empty(&l->signal->listeners)) {
		HASH_DEL(((struct di_object_internal *)l->signal->owner)->signals, l->signal);
		// Don't call deleter for internal signal names
		if (strncmp(l->signal->name, "__", 2) != 0) {
			bool handler_found;
			call_handler_with_fallback(l->signal->owner, "__del_signal",
			                           l->signal->name,
			                           (struct di_variant){NULL, DI_LAST_TYPE},
			                           NULL, NULL, &handler_found);
			di_unref_object((void *)l->signal->owner);
		}
		free(l->signal->name);
		free(l->signal);
	}

	l->signal = NULL;
	if (l->handler) {
		di_unref_object(l->handler);
	}
	l->handler = NULL;
	di_unref_object((void *)l);
	return 0;
}

PUBLIC_DEAI_API int di_emitn(struct di_object *o, const char *name, struct di_tuple t) {
	if (t.length > MAX_NARGS) {
		return -E2BIG;
	}

	assert(t.length == 0 || (t.elements != NULL));

	struct di_signal *sig;
	HASH_FIND_STR(((struct di_object_internal *)o)->signals, name, sig);
	if (!sig) {
		return 0;
	}

	int cnt = 0;
	struct di_listener *l, **all_l = tmalloc(struct di_listener *, sig->nlisteners);

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
		if (!l->handler) {
			// Listener stopped/null listener
			goto next;
		}
		di_type_t rtype;
		union di_value ret;
		int rc =
		    ((struct di_object_internal *)l->handler)->call(l->handler, &rtype, &ret, t);

		if (rc == 0) {
			di_free_value(rtype, &ret);
		} else {
			fprintf(stderr, "Failed to call a listener callback: %s\n",
			        strerror(-rc));
		}
		if (l->once) {
			di_stop_listener(l);
		}
	next:
		di_unref_object((void *)l);
	}
	free(all_l);
	di_unref_object(o);
	return 0;
}

#undef is_destroy

#ifdef TRACK_OBJECTS
void di_dump_objects(void) {
	struct di_object_internal *i;
	list_for_each_entry (i, &all_objects, siblings) {
		fprintf(stderr, "%p, ref count: %lu strong %lu weak, type: %s\n", i,
		        i->ref_count, i->weak_ref_count, di_get_type((void *)i));
		for (struct di_member *m = i->members; m != NULL; m = m->hh.next) {
			fprintf(stderr, "\tmember: %s, type: %s\n", m->name,
			        di_type_to_string(m->type));
		}
		for (struct di_signal *s = i->signals; s != NULL; s = s->hh.next) {
			fprintf(stderr, "\tsignal: %s, nlisteners: %d\n", s->name, s->nlisteners);
		}
	}
}
#endif
