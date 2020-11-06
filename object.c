/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai/builtins/log.h>
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

struct di_signal {
	struct di_string name;
	int nlisteners;
	struct di_weak_object *owner;
	struct list_head listeners;
	UT_hash_handle hh;
};

// This is essentially a fat weak reference, from object to its listeners.
struct di_listener {
	struct di_weak_object *listen_handle;
	struct list_head siblings;
};

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

static bool di_is_internal(struct di_string s) {
	return s.length >= 2 && strncmp(s.data, "__", 2) == 0;
}

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
	int fnname(struct di_object *self, struct di_string name, di_type_t *rt,         \
	           union di_value *ret, struct di_tuple args, bool *called) {            \
		struct di_object *val;                                                   \
		*called = false;                                                         \
		int rc = getter(self, name, DI_TYPE_OBJECT, (union di_value *)&val);     \
		if (rc != 0) {                                                           \
			return rc;                                                       \
		}                                                                        \
		return di_call_internal(self, val, rt, ret, args, called);               \
	}

gen_callx(di_callx, di_getxt);
gen_callx(di_rawcallxn, di_rawgetxt);

/// Call "<prefix>_<name>" with "<prefix>" as fallback
///
/// @param[out] found whether a handler is found
static int call_handler_with_fallback(struct di_object *nonnull o,
                                      const char *nonnull prefix, struct di_string name,
                                      struct di_variant arg, di_type_t *nullable rtype,
                                      union di_value *nullable ret, bool *found) {
	*found = false;
	// Internal names doesn't go through handler
	if (di_is_internal(name)) {
		return -ENOENT;
	}

	char *buf;
	asprintf(&buf, "%s_%.*s", prefix, (int)name.length, name.data);
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
	int rc = di_rawcallxn(o, di_string_borrow(buf), &rtype2, &ret2, tmp, found);
	free(buf);

	if (*found) {
		goto ret;
	}

	tmp.length++;
	if (tmp.length > 1) {
		args[1] = args[0];
	}
	args[0] = (struct di_variant){
	    .type = DI_TYPE_STRING,
	    .value = &(union di_value){.string = name},
	};

	rc = di_rawcallxn(o, di_string_borrow(prefix), &rtype2, &ret2, tmp, found);
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

int di_setx(struct di_object *o, struct di_string prop, di_type_t type, const void *val) {
	// If a setter is present, we just call that and we are done.
	bool handler_found;
	int rc = call_handler_with_fallback(o, "__set", prop,
	                                    (struct di_variant){(union di_value *)val, type},
	                                    NULL, NULL, &handler_found);
	if (handler_found) {
		return rc;
	}

	// Call the deleter if present
	rc = call_handler_with_fallback(o, "__delete", prop,
	                                (struct di_variant){NULL, DI_LAST_TYPE}, NULL,
	                                NULL, &handler_found);
	if (handler_found && rc != 0) {
		return rc;
	}

	// Finally, replace the value
	auto mem = di_lookup(o, prop);
	if (mem) {
		// the old member still exists, we need to drop the old value
		di_free_value(mem->type, mem->data);

		mem->data = realloc(mem->data, di_sizeof_type(type));
		di_copy_value(mem->type, mem->data, val);
		return 0;
	}

	return di_add_member_clone(o, prop, type, val);
}

int di_rawgetx(struct di_object *o, struct di_string prop, di_type_t *type, union di_value *ret) {
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
		if (inner) {
			var->type = inner->variant.type;
			var->value = inner->variant.value;
			free(inner);
		}
	}
}

int di_getx(struct di_object *o, struct di_string prop, di_type_t *type, union di_value *ret) {
	int rc = di_rawgetx(o, prop, type, ret);
	if (rc == 0) {
		return 0;
	}

	bool handler_found;
	rc = call_handler_with_fallback(o, "__get", prop,
	                                (struct di_variant){NULL, DI_LAST_TYPE}, type,
	                                ret, &handler_found);
	if (rc != 0) {
		return rc;
	}

	if (*type == DI_LAST_TYPE) {
		return -ENOENT;
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

#define gen_tfunc(name, getter)                                                          \
	int name(struct di_object *o, struct di_string prop, di_type_t rtype,            \
	         union di_value *ret) {                                                  \
		union di_value ret2;                                                     \
		di_type_t rt;                                                            \
		int rc = getter(o, prop, &rt, &ret2);                                    \
		if (rc != 0) {                                                           \
			return rc;                                                       \
		}                                                                        \
		bool cloned = false;                                                     \
		rc = di_type_conversion(rt, &ret2, rtype, ret, false, &cloned);          \
		/* Free the original if it is cloned while being converted, or     */    \
		/* conversion failed in which case we don't need the value anymore */    \
		if (cloned || rc != 0) {                                                 \
			di_free_value(rt, &ret2);                                        \
		}                                                                        \
		return rc;                                                               \
	}

gen_tfunc(di_getxt, di_getx);
gen_tfunc(di_rawgetxt, di_rawgetx);

int di_set_type(struct di_object *o, const char *type) {
	di_remove_member_raw(o, di_string_borrow("__type"));
	return di_add_member_clonev(o, di_string_borrow("__type"), DI_TYPE_STRING_LITERAL, type);
}

const char *di_get_type(struct di_object *o) {
	const char *ret;
	int rc = di_rawgetxt(o, di_string_borrow("__type"), DI_TYPE_STRING_LITERAL,
	                     (union di_value *)&ret);
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
	DI_CHECK_OK(posix_memalign((void **)&obj, alignment, sz));
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

	auto weak = di_weakly_ref_object((struct di_object *)di);
	di_member(pm, "__deai", weak);

	return (void *)pm;
}

struct di_module *di_new_module(struct deai *di) {
	return di_new_module_with_size(di, sizeof(struct di_module));
}

static void di_remove_member_raw_impl(struct di_object_internal *obj, struct di_member *m) {
	HASH_DEL(*(struct di_member **)&obj->members, m);

	di_free_value(m->type, m->data);
	free(m->data);
	di_free_string(m->name);
	free(m);
}

int di_remove_member_raw(struct di_object *obj, struct di_string name) {
	auto m = di_lookup(obj, name);
	if (!m) {
		return -ENOENT;
	}

	di_remove_member_raw_impl((struct di_object_internal *)obj, (void *)m);
	return 0;
}

int di_remove_member(struct di_object *obj, struct di_string name) {
	bool handler_found;
	int rc2 = call_handler_with_fallback(obj, "__delete", name,
	                                     (struct di_variant){NULL, DI_LAST_TYPE},
	                                     NULL, NULL, &handler_found);
	if (handler_found) {
		return rc2;
	}

	return di_remove_member_raw(obj, name);
}

static void _di_finalize_object(struct di_object_internal *obj) {
	// Call dtor before removing members and signals, so the dtor can still make use
	// of whatever is stored in the object, and emit more signals.
	// But this also means signal and member deleters won't be called for them.
	if (obj->dtor) {
		auto tmp = obj->dtor;
		// Never call dtor more than once
		obj->dtor = NULL;
		tmp((struct di_object *)obj);
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
		di_remove_member_raw_impl(obj, m);
		m = next_m;
	}
}

void di_finalize_object(struct di_object *_obj) {
	// Prevent object from being freed while we are finalizing
	// XXX Hacky: if things are referenced properly, this shouldn't be necessary
	di_ref_object(_obj);
	_di_finalize_object((struct di_object_internal *)_obj);

	di_unref_object(_obj);
}

// Try to never call destroy twice on something. Although it's fine to do so
static void di_destroy_object(struct di_object *_obj) {
	auto obj = (struct di_object_internal *)_obj;

	// Prevent destroy from being called while we are destroying
	// XXX Hacky: if things are referenced properly, this shouldn't be necessary
	di_ref_object(_obj);
	if (obj->destroyed) {
		di_log_va(log_module, DI_LOG_WARN, "warning: destroy object multiple times\n");
	}
	obj->destroyed = 1;

	_di_finalize_object(obj);

	struct di_signal *sig, *tmpsig;
	HASH_ITER (hh, obj->signals, sig, tmpsig) {
		// Detach the signal structs from this object, don't free them.
		// The signal structs are collectively owned by the listener structs,
		// which in turn is owned by the listen handles, and will be freed when
		// the listen handles are dropped.
		HASH_DEL(obj->signals, sig);
	}

	di_unref_object(_obj);
}

struct di_object *di_ref_object(struct di_object *_obj) {
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

	if (!m->name.data) {
		return -EINVAL;
	}

	HASH_FIND(hh, obj->members, m->name.data, m->name.length, om);
	if (om) {
		return -EEXIST;
	}

	static const char *const getter_prefix = "__get_";
	const size_t getter_prefix_len = strlen(getter_prefix);
	static const char *const setter_prefix = "__set_";
	const size_t setter_prefix_len = strlen(setter_prefix);
	static const char *const deleter_prefix = "__delete_";
	const size_t deleter_prefix_len = strlen(setter_prefix);

	const char *real_name = NULL;
	size_t real_name_len = 0;
	if (m->name.length >= getter_prefix_len &&
	    strncmp(m->name.data, getter_prefix, getter_prefix_len) == 0) {
		real_name = m->name.data + getter_prefix_len;
		real_name_len = m->name.length - getter_prefix_len;
	} else if (m->name.length >= setter_prefix_len &&
	           strncmp(m->name.data, setter_prefix, setter_prefix_len) == 0) {
		real_name = m->name.data + setter_prefix_len;
		real_name_len = m->name.length - setter_prefix_len;
	} else if (m->name.length >= deleter_prefix_len &&
	           strncmp(m->name.data, deleter_prefix, deleter_prefix_len) == 0) {
		real_name = m->name.data + deleter_prefix_len;
		real_name_len = m->name.length - deleter_prefix_len;
	}

	if (real_name_len >= 2 && strncmp(real_name, "__", 2) == 0) {
		return -EINVAL;
	}
	return 0;
}

static int di_insert_member(struct di_object_internal *obj, struct di_member *m) {
	int ret = check_new_member(obj, (void *)m);
	if (ret != 0) {
		return ret;
	}

	HASH_ADD_KEYPTR(hh, obj->members, m->name.data, m->name.length, m);
	return 0;
}

static int
di_add_member(struct di_object_internal *o, struct di_string name, di_type_t t, void *v) {
	if (!name.data) {
		return -EINVAL;
	}

	auto m = tmalloc(struct di_member, 1);
	m->type = t;
	m->data = v;
	m->name = di_clone_string(name);

	int ret = di_insert_member(o, m);
	if (ret != 0) {
		di_free_value(t, v);
		free(v);

		di_free_string(m->name);
		free(m);
	}
	return ret;
}

int di_add_member_clone(struct di_object *o, struct di_string name, di_type_t t,
                        const void *value) {
	if (di_sizeof_type(t) == 0) {
		return -EINVAL;
	}

	void *copy = calloc(1, di_sizeof_type(t));
	di_copy_value(t, copy, value);

	return di_add_member((struct di_object_internal *)o, name, t, copy);
}

int di_add_member_clonev(struct di_object *o, struct di_string name, di_type_t t, ...) {
	if (di_sizeof_type(t) == 0) {
		return -EINVAL;
	}

	union di_value nv;
	va_list ap;

	va_start(ap, t);
	va_arg_with_di_type(ap, t, &nv);
	va_end(ap);

	return di_add_member_clone(o, name, t, &nv);
}

int di_add_member_move(struct di_object *o, struct di_string name, di_type_t *t, void *addr) {
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

struct di_member *di_lookup(struct di_object *_obj, struct di_string name) {
	if (name.data == NULL) {
		return NULL;
	}

	auto obj = (struct di_object_internal *)_obj;
	struct di_member *ret = NULL;
	HASH_FIND(hh, obj->members, name.data, name.length, ret);
	return (void *)ret;
}

void di_set_object_dtor(struct di_object *nonnull obj, di_dtor_fn_t nullable dtor) {
	auto internal = (struct di_object_internal *)obj;
	internal->dtor = dtor;
}

void di_set_object_call(struct di_object *nonnull obj, di_call_fn_t nullable call) {
	auto internal = (struct di_object_internal *)obj;
	internal->call = call;
}

bool di_is_object_callable(struct di_object *nonnull obj) {
	auto internal = (struct di_object_internal *)obj;
	return internal->call != NULL;
}

void di_free_tuple(struct di_tuple t) {
	for (int i = 0; i < t.length; i++) {
		di_free_value(DI_TYPE_VARIANT, (union di_value *)&t.elements[i]);
	}
	free(t.elements);
}

void di_free_array(struct di_array arr) {
	if (arr.length == 0) {
		DI_CHECK(arr.arr == NULL);
		return;
	}
	size_t step = di_sizeof_type(arr.elem_type);
	for (int i = 0; i < arr.length; i++) {
		di_free_value(arr.elem_type, arr.arr + step * i);
	}
	free(arr.arr);
}

void di_free_value(di_type_t t, union di_value *value_ptr) {
	if (t == DI_TYPE_NIL) {
		return;
	}

	// If t != DI_TYPE_UINT, then `ptr_` cannot be NULL
	union di_value *nonnull val = value_ptr;
	struct di_object *nonnull obj;
	switch (t) {
	case DI_TYPE_ARRAY:
		di_free_array(val->array);
		break;
	case DI_TYPE_TUPLE:
		di_free_tuple(val->tuple);
		break;
	case DI_TYPE_STRING:
		di_free_string(val->string);
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

void di_copy_value(di_type_t t, void *dst, const void *src) {
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
		dstval->string = di_clone_string(srcval->string);
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

struct di_listen_handle {
	struct di_object_internal;
	struct di_signal *nonnull signal;

	// listen_handle owns the listen_entry
	struct di_listener *nonnull listen_entry;
};

static void di_listen_handle_dtor(struct di_object *nonnull obj) {
	auto lh = (struct di_listen_handle *)obj;
	DI_CHECK(lh->signal != PTR_POISON);
	DI_CHECK(lh->listen_entry != PTR_POISON);

	lh->signal->nlisteners--;
	list_del(&lh->listen_entry->siblings);
	if (list_empty(&lh->signal->listeners)) {
		// Owner might have already died. In that case we just free the signal
		// struct. If the owner is still alive, we also call its signal deleter
		// if this is the last listener of signal, and detach the signal struct
		// from the owner's signals.
		di_object_with_cleanup owner = di_upgrade_weak_ref(lh->signal->owner);
		auto owner_internal = (struct di_object_internal *)owner;
		if (owner_internal) {
			HASH_DEL(owner_internal->signals, lh->signal);

			// Don't call deleter for internal signal names
			if (!di_is_internal(lh->signal->name)) {
				bool handler_found;
				call_handler_with_fallback(
				    owner, "__del_signal", lh->signal->name,
				    (struct di_variant){NULL, DI_LAST_TYPE}, NULL, NULL,
				    &handler_found);
			}
		}
		di_drop_weak_ref(&lh->signal->owner);
		di_free_string(lh->signal->name);
		free(lh->signal);
	}

	lh->signal = PTR_POISON;
	di_drop_weak_ref(&lh->listen_entry->listen_handle);
	free(lh->listen_entry);
	lh->listen_entry = PTR_POISON;
}

struct di_object *
di_listen_to(struct di_object *_obj, struct di_string name, struct di_object *h) {
	auto obj = (struct di_object_internal *)_obj;
	assert(!obj->destroyed);

	struct di_signal *sig = NULL;
	HASH_FIND(hh, obj->signals, name.data, name.length, sig);
	if (!sig) {
		sig = tmalloc(struct di_signal, 1);
		sig->name = di_clone_string(name);
		sig->owner = di_weakly_ref_object(_obj);

		INIT_LIST_HEAD(&sig->listeners);
		HASH_ADD_KEYPTR(hh, obj->signals, sig->name.data, sig->name.length, sig);
		if (!di_is_internal(name)) {
			bool handler_found;
			call_handler_with_fallback(_obj, "__new_signal", sig->name,
			                           (struct di_variant){NULL, DI_LAST_TYPE},
			                           NULL, NULL, &handler_found);
		}
	}

	auto l = tmalloc(struct di_listener, 1);
	auto listen_handle = di_new_object_with_type(struct di_listen_handle);
	DI_CHECK_OK(di_set_type((void *)listen_handle, "deai:ListenHandle"));
	l->listen_handle = di_weakly_ref_object((struct di_object *)listen_handle);
	listen_handle->listen_entry = l;

	listen_handle->dtor = di_listen_handle_dtor;
	listen_handle->signal = sig;

	di_member_clone(listen_handle, "__handler", h);

	list_add(&l->siblings, &sig->listeners);
	sig->nlisteners++;

	return (struct di_object *)listen_handle;
}

int di_emitn(struct di_object *o, struct di_string name, struct di_tuple args) {
	if (args.length > MAX_NARGS) {
		return -E2BIG;
	}

	assert(args.length == 0 || (args.elements != NULL));

	struct di_signal *sig;
	HASH_FIND(hh, ((struct di_object_internal *)o)->signals, name.data, name.length, sig);
	if (!sig) {
		return 0;
	}

	int cnt = 0;
	struct di_weak_object **all_handle = tmalloc(struct di_weak_object *, sig->nlisteners);

	{
		// Listen handles can be dropped during emission, in which case their
		// listener structs will be freed too. And there is no limit on which
		// handle can be dropped by which handler, so list_for_each_entry_safe is
		// not enough.
		//
		// So first we retrieve the list of listen handles we need, and stop using
		// the listener structs from here on.
		struct di_listener *l;
		list_for_each_entry (l, &sig->listeners, siblings) {
			di_copy_value(DI_TYPE_WEAK_OBJECT, &all_handle[cnt++], &l->listen_handle);
		}

		// Q: why don't we just grab the list of handlers instead?
		// A: because what we really care about is whether the listen handle is
		//    alive or not. the handler object could be kept alive by something
		//    else even though the listener has stopped. in that case we will
		//    unnecessarily call the handler.
	}

	assert(cnt == sig->nlisteners);
	for (int i = 0; i < cnt; i++) {
		auto handle = (struct di_listen_handle *)di_upgrade_weak_ref(all_handle[i]);
		if (!handle) {
			// Listener has stopped, because the listen handle has been
			// dropped
			continue;
		}

		struct di_object *handler;
		DI_CHECK_OK(di_get(handle, "__handler", handler));

		// Drop the handle early, we have a strong reference to handler, so we
		// don't need the handle anymore. This also allows the handle to be
		// dropped during the handler call. If we keep a ref to the handle,
		// dropping the handle in the handler won't take immediate effect until
		// the handler returns, which is undesirable.
		di_unref_object((struct di_object *)handle);
		handle = NULL;

		di_type_t rtype;
		union di_value ret;
		int rc = di_call_objectt(handler, &rtype, &ret, args);

		di_unref_object(handler);
		di_drop_weak_ref(&all_handle[i]);

		if (rc == 0) {
			if (rtype == DI_TYPE_OBJECT) {
				struct di_string errmsg;
				if (di_get(ret.object, "errmsg", errmsg) == 0) {
					di_log_va(log_module, DI_LOG_ERROR,
					          "Error arose when calling signal "
					          "handler: %.*s\n",
					          (int)errmsg.length, errmsg.data);
					di_free_string(errmsg);
				}
			}
			di_free_value(rtype, &ret);
		} else {
			di_log_va(log_module, DI_LOG_ERROR,
			          "Failed to call a listener callback: %s\n", strerror(-rc));
		}
	}
	free(all_handle);
	return 0;
}

#undef is_destroy

struct di_roots *roots;
struct di_object *di_get_roots(void) {
	return (struct di_object *)roots;
}

#ifdef TRACK_OBJECTS
static void di_dump_object(struct di_object_internal *obj) {
	fprintf(stderr, "%p, ref count: %lu strong %lu weak (live: %d), type: %s\n", obj,
	        obj->ref_count, obj->weak_ref_count, obj->mark, di_get_type((void *)obj));
	for (struct di_member *m = obj->members; m != NULL; m = m->hh.next) {
		fprintf(stderr, "\tmember: %.*s, type: %s", (int)m->name.length,
		        m->name.data, di_type_to_string(m->type));
		if (m->type == DI_TYPE_OBJECT) {
			union di_value *val = m->data;
			fprintf(stderr, " (%s)", di_get_type(val->object));
			auto obj_internal = (struct di_object_internal *)val->object;
			obj_internal->excess_ref_count--;
		}
		fprintf(stderr, "\n");
	}
	for (struct di_signal *s = obj->signals; s != NULL; s = s->hh.next) {
		fprintf(stderr, "\tsignal: %.*s, nlisteners: %d\n", (int)s->name.length,
		        s->name.data, s->nlisteners);
	}
}
void di_dump_objects(void) {
	struct di_object_internal *i;
	list_for_each_entry (i, &all_objects, siblings) {
		i->excess_ref_count = i->ref_count;
	}

	list_for_each_entry (i, &all_objects, siblings) { di_dump_object(i); }

	// Account for references from the roots
	if (roots != NULL) {
		roots->excess_ref_count--;
		for (auto root = roots->anonymous_roots; root; root = root->hh.next) {
			auto obj_internal = (struct di_object_internal *)root->obj;
			obj_internal->excess_ref_count--;
		}
	}

	list_for_each_entry (i, &all_objects, siblings) {
		// Excess ref count doesn't count references in roots. Object added as
		// roots will have 1 excess_ref_count per root.
		fprintf(stderr, "%p, excess_ref_count: %lu/%lu\n", i, i->excess_ref_count,
		        i->ref_count);
	}
}

static void di_mark_and_sweep_dfs(struct di_object_internal *o) {
	if (o->mark != 0) {
		if (o->mark == 1) {
			di_log_va(log_module, DI_LOG_WARN, "Reference cycle detected\n");
		}
		return;
	}

	o->mark = 1;
	for (auto i = o->members; i != NULL; i = i->hh.next) {
		if (i->type != DI_TYPE_OBJECT) {
			continue;
		}
		union di_value *val = i->data;
		di_mark_and_sweep_dfs((struct di_object_internal *)val->object);
	}

	o->mark = 2;
}

bool di_mark_and_sweep(void) {
	struct di_object_internal *i;
	list_for_each_entry (i, &all_objects, siblings) { i->mark = 0; }
	di_mark_and_sweep_dfs((struct di_object_internal *)roots);
	for (auto root = roots->anonymous_roots; root != NULL; root = root->hh.next) {
		di_mark_and_sweep_dfs((struct di_object_internal *)root->obj);
	}
	list_for_each_entry (i, &all_objects, siblings) {
		if (i->mark == 0) {
			return true;
		}
	}
	return false;
}

#endif
