/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/mman.h>

#include <deai/callable.h>
#include <deai/object.h>

#include "config.h"
#include "di_internal.h"
#include "utils.h"

#define PUBLIC __attribute__((visibility("default")))

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

PUBLIC int
di_rawcallxn(struct di_object *o, const char *name, di_type_t *rt, void **ret,
             int nargs, const di_type_t *ats, const void *const *args) {
	const void *val;
	int rc = di_rawgetxt(o, name, DI_TYPE_OBJECT, &val);
	if (rc != 0)
		return rc;

	auto m = *(struct di_object **)val;
	free((void *)val);

	if (!m->call)
		return -EINVAL;

	rc = m->call(m, rt, ret, nargs, ats, args);

	di_unref_object(m);
	return rc;
}

PUBLIC int
di_setx(struct di_object *o, const char *name, di_type_t type, const void *val) {
	auto mem = di_find_member(o, name);
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

	char *buf;
	void *ret;
	di_type_t rtype;
	asprintf(&buf, "__set_%s", name);

	int rc2 = di_rawcallxn(o, buf, &rtype, &ret, 1, (di_type_t[]){type},
	                       (const void *[]){val});
	free(buf);
	if (rc2 != -ENOENT)
		return rc2;

	rc2 = di_rawcallxn(o, "__set", &rtype, &ret, 2,
	                   (di_type_t[]){DI_TYPE_STRING, type},
	                   (const void *[]){name, val});
	if (rc2 != -ENOENT)
		return rc2;
	return rc;
}

PUBLIC int
di_rawgetx(struct di_object *o, const char *name, di_type_t *type, const void **ret) {
	auto m = di_find_member(o, name);
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
	return obj;
}

PUBLIC struct di_module *di_new_module(size_t size) {
	if (size < sizeof(struct di_module))
		return NULL;

	struct di_module_internal *pm = (void *)di_new_object(size);

	di_set_type((void *)pm, "module");

	return (void *)pm;
}

// Must be called holding external references. i.e.
// __dtor shouldn't cause the reference count to drop to 0
PUBLIC void di_destroy_object(struct di_object *_obj) {
	struct di_object *obj = (void *)_obj;
	assert(obj->destroyed != 2);

	if (obj->destroyed)
		return;

	obj->destroyed = 2;

	// TODO: Send __destroyed signal

	if (obj->dtor)
		obj->dtor(obj);
	struct di_member_internal *m = (void *)obj->members;
	while (m) {
		auto next_m = m->hh.next;
		if (m->type != DI_TYPE_OBJECT)
			fprintf(stderr, "removing member %s\n", m->name);
		else
			fprintf(stderr, "removing member %s(%d)\n", m->name,
			        *(struct di_object **)m->data
			            ? (*(struct di_object **)m->data)->ref_count
			            : -1);
		HASH_DEL(*(struct di_member_internal **)&obj->members, m);

		di_free_value(m->type, m->data);

		if (m->own)
			free(m->data);
		free(m->name);
		free(m);
		m = next_m;
	}

	obj->destroyed = 1;
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

static struct di_member *di_alloc_member(void) {
	return (void *)calloc(1, sizeof(struct di_member_internal));
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

PUBLIC int di_add_address_member(struct di_object *o, const char *name,
                                 bool writable, di_type_t t, void *addr) {
	return _di_add_member(o, name, writable, false, t, addr);
}

PUBLIC struct di_member *di_find_member(struct di_object *o, const char *name) {
	struct di_member_internal *ret = NULL;

	do {
		ret = NULL;
		if (o->members != NULL) {
			unsigned _hf_bkt, _hf_hashv;
			HASH_FCN(name, strlen(name), ((struct di_member_internal *)o->members)->hh.tbl->num_buckets,
			         _hf_hashv, _hf_bkt);
			if (HASH_BLOOM_TEST(((struct di_member_internal *)o->members)->hh.tbl, _hf_hashv) != 0) {
				HASH_FIND_IN_BKT(((struct di_member_internal *)o->members)->hh.tbl, hh,
				                 ((struct di_member_internal *)o->members)->hh.tbl->buckets[_hf_bkt],
				                 name, strlen(name), ret);
			}
		}
	} while (0);
	return (void *)ret;
}

#define chknull(v) if ((*(void **)(v)) != NULL)

PUBLIC void di_free_array(struct di_array arr) {
	size_t step = di_sizeof_type(arr.elem_type);
	for (int i = 0; i < arr.length; i++)
		di_free_value(arr.elem_type, arr.arr + step * i);
	free(arr.arr);
}

PUBLIC void di_free_value(di_type_t t, void *ret) {
	switch (t) {
	case DI_TYPE_ARRAY: di_free_array(*(struct di_array *)ret); break;
	case DI_TYPE_STRING: chknull(ret) free(*(char **)ret); break;
	case DI_TYPE_OBJECT:
		chknull(ret) di_unref_object(*(struct di_object **)ret);
		break;
	default: break;
	}
}

PUBLIC void di_copy_value(di_type_t t, void *dst, const void *src) {
	const struct di_array *arr;
	void *d;
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
	case DI_TYPE_STRING:
		*(const char **)dst = strdup(*(const char **)src);
		break;
	case DI_TYPE_OBJECT:
		di_ref_object(*(struct di_object **)src);
		*(struct di_object **)dst = *(struct di_object **)src;
		break;
	default: memmove(dst, src, di_sizeof_type(t)); break;
	}
}
