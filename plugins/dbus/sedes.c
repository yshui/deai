#include <assert.h>

#include "common.h"
#include "list.h"
#include "sedes.h"
static di_type_t _dbus_type_to_di(int type) {
	switch (type) {
	case DBUS_TYPE_BOOLEAN: return DI_TYPE_BOOL;
	case DBUS_TYPE_INT16:
	case DBUS_TYPE_INT32:
	case DBUS_TYPE_INT64: return DI_TYPE_INT;
	case DBUS_TYPE_UINT16:
	case DBUS_TYPE_UINT32:
	case DBUS_TYPE_UINT64: return DI_TYPE_UINT;
	case DBUS_TYPE_DOUBLE: return DI_TYPE_FLOAT;
	case DBUS_TYPE_STRING: return DI_TYPE_STRING;
	case DBUS_TYPE_UNIX_FD:
		// TODO
		return DI_TYPE_INT;
	case DBUS_TYPE_ARRAY: return DI_TYPE_ARRAY;
	case DBUS_TYPE_STRUCT: return DI_TYPE_TUPLE;
	default: return DI_LAST_TYPE;
	}
}

#define _DESERIAL(typeid, type, tgt, l)                                             \
	case typeid:                                                                \
		do {                                                                \
			type __o;                                                   \
			dbus_message_iter_get_basic(i, &__o);                       \
			tgt = __o;                                                  \
			goto l;                                                     \
		} while (0)

static void
_dbus_deserialize_basic(DBusMessageIter *i, void *retp, di_type_t *otype, int type) {
	int64_t i64;
	uint64_t u64;
	double d;
	const char *str;
	bool b;
	switch (type) {
		_DESERIAL(DBUS_TYPE_BOOLEAN, dbus_bool_t, b, dbool);
		_DESERIAL(DBUS_TYPE_INT16, dbus_int16_t, i64, dint);
		_DESERIAL(DBUS_TYPE_INT32, dbus_int32_t, i64, dint);
		_DESERIAL(DBUS_TYPE_UNIX_FD, dbus_int32_t, i64, dint);
		_DESERIAL(DBUS_TYPE_INT64, dbus_int64_t, i64, dint);
		_DESERIAL(DBUS_TYPE_UINT16, dbus_uint16_t, u64, duint);
		_DESERIAL(DBUS_TYPE_UINT32, dbus_uint32_t, u64, duint);
		_DESERIAL(DBUS_TYPE_UINT64, dbus_uint64_t, u64, duint);
		_DESERIAL(DBUS_TYPE_DOUBLE, double, d, dfloat);
		_DESERIAL(DBUS_TYPE_STRING, const char *, str, dstr);
	}
dbool:
	*(bool *)retp = b;
	*otype = DI_TYPE_BOOL;
	return;
dint:
	*(int64_t *)retp = i64;
	*otype = DI_TYPE_INT;
	return;
duint:
	*(uint64_t *)retp = u64;
	*otype = DI_TYPE_UINT;
	return;
dfloat:
	*(double *)retp = d;
	*otype = DI_TYPE_FLOAT;
	return;
dstr:
	*(char **)retp = strdup(str);
	*otype = DI_TYPE_STRING;
	return;
}

#undef _DESERIAL

static void _dbus_deserialize_one(DBusMessageIter *, void *, di_type_t *, int);

// Deserialize an array. `i' is the iterator, already recursed into the array
// `type' is the array element type
static void _dbus_deserialize_array(DBusMessageIter *i, struct di_array *retp,
                                    int type, int length) {
	if (dbus_type_is_fixed(type)) {
		struct di_array ret;
		int length;
		dbus_message_iter_get_fixed_array(i, &ret.arr, &length);
		ret.length = length;
		*retp = ret;
		return;
	}

	struct di_array ret;
	ret.elem_type = _dbus_type_to_di(type);

	size_t esize = di_sizeof_type(ret.elem_type);
	ret.length = length;
	if (ret.elem_type >= DI_LAST_TYPE) {
		*retp = DI_ARRAY_NIL;
		return;
	}
	ret.arr = calloc(ret.length, esize);
	for (int x = 0; x < ret.length; x++) {
		di_type_t _;
		_dbus_deserialize_one(i, ret.arr + esize * x, &_, type);
		dbus_message_iter_next(i);
	}
	*retp = ret;
}

void _dbus_deserialize_tuple(DBusMessageIter *i, void *retp) {
	struct di_tuple t = {0};
	DBusMessageIter tmpi = *i;
	while (dbus_message_iter_get_arg_type(&tmpi) != DBUS_TYPE_INVALID) {
		dbus_message_iter_next(&tmpi);
		t.length++;
	}
	t.tuple = tmalloc(void *, t.length);
	t.elem_type = tmalloc(di_type_t, t.length);
	for (int x = 0; x < t.length; x++) {
		int type = dbus_message_iter_get_arg_type(i);
		t.elem_type[x] = _dbus_type_to_di(type);

		t.tuple[x] = calloc(1, di_sizeof_type(t.elem_type[x]));
		di_type_t rtype;
		_dbus_deserialize_one(i, t.tuple[x], &rtype, type);

		// Dict type can't be discerned from the outer type alone (which
		// would be array).
		// If deserialize_one returns an object and we expect an array,
		// that means it's a dbus dict.
		if (rtype == DI_TYPE_OBJECT && t.elem_type[x] == DI_TYPE_ARRAY)
			t.elem_type[x] = rtype;
		assert(rtype == t.elem_type[x]);
		dbus_message_iter_next(i);
	}
	*(struct di_tuple *)retp = t;
}

static void _dbus_deserialize_dict(DBusMessageIter *i, void *retp, int length) {
	auto o = di_new_object_with_type(struct di_object);
	for (int x = 0; x < length; x++) {
		struct di_tuple t;
		DBusMessageIter i2;
		dbus_message_iter_recurse(i, &i2);
		_dbus_deserialize_tuple(&i2, &t);
		assert(t.length == 2);
		assert(t.elem_type[0] == DI_TYPE_STRING);
		di_add_member_move(o, *(const char **)t.tuple[0], false,
		                   &t.elem_type[1], t.tuple[1]);
		di_free_tuple(t);
	}
	*(struct di_object **)retp = o;
}

static void
_dbus_deserialize_one(DBusMessageIter *i, void *retp, di_type_t *otype, int type) {
	if (dbus_type_is_basic(type))
		return _dbus_deserialize_basic(i, retp, otype, type);

	if (type == DBUS_TYPE_ARRAY) {
		DBusMessageIter i2;
		dbus_message_iter_recurse(i, &i2);
		int type2 = dbus_message_iter_get_arg_type(&i2);
		// deserialize dict with string keys as object
		if (type2 == DBUS_TYPE_DICT_ENTRY) {
			DBusMessageIter i3;
			dbus_message_iter_recurse(&i2, &i3);
			int type3 = dbus_message_iter_get_arg_type(&i3);
			if (type3 == DBUS_TYPE_STRING) {
				*otype = DI_TYPE_OBJECT;
				return _dbus_deserialize_dict(&i2, retp, dbus_message_iter_get_element_count(i));
			}
		}
		*otype = DI_TYPE_ARRAY;
		return _dbus_deserialize_array(
		    &i2, retp, type2, dbus_message_iter_get_element_count(i));
	}

	if (type == DBUS_TYPE_STRUCT || type == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter i2;
		dbus_message_iter_recurse(i, &i2);
		*otype = DI_TYPE_TUPLE;
		_dbus_deserialize_tuple(&i2, retp);
	}
}

static int _di_type_to_dbus_basic(di_type_t type) {
	static_assert(sizeof(int) == 4, "NINT is not INT32");
	static_assert(sizeof(unsigned int) == 4, "NUINT is not UINT32");
	switch (type) {
	case DI_TYPE_BOOL: return DBUS_TYPE_BOOLEAN;
	case DI_TYPE_INT: return DBUS_TYPE_INT64;
	case DI_TYPE_UINT: return DBUS_TYPE_UINT64;
	case DI_TYPE_NINT: return DBUS_TYPE_INT32;
	case DI_TYPE_NUINT: return DBUS_TYPE_UINT32;
	case DI_TYPE_FLOAT: return DBUS_TYPE_DOUBLE;
	case DI_TYPE_STRING: return DBUS_TYPE_STRING;
	case DI_TYPE_ARRAY: return DBUS_TYPE_ARRAY;
	case DI_TYPE_TUPLE: return DBUS_TYPE_STRUCT;
	default: return DBUS_TYPE_INVALID;
	}
}

struct _dbus_signature {
	char *current;
	int length;
	int nchild;
	struct _dbus_signature *child;
};

static int _type_signature_length_of_di_value(di_type_t type, void *d) {
	int dtype = _di_type_to_dbus_basic(type);
	if (dbus_type_is_basic(dtype))
		return 1;
	if (type == DI_TYPE_ARRAY) {
		struct di_array *arr = d;
		int v0 = _type_signature_length_of_di_value(arr->elem_type, arr->arr);
		int step = di_sizeof_type(arr->elem_type);
		for (int i = 1; i < arr->elem_type; i++) {
			int v = _type_signature_length_of_di_value(
			    arr->elem_type, arr->arr + step * i);
			if (v != v0)
				return -1;
		}
		return 1 + v0;
	}
	if (type == DI_TYPE_TUPLE) {
		struct di_tuple *t = d;
		int ret = 0;
		for (int i = 0; i < t->length; i++) {
			int tmp = _type_signature_length_of_di_value(t->elem_type[i],
			                                             t->tuple[i]);
			if (tmp < 0)
				return tmp;
			ret += tmp;
		}
		return ret + 2;
	}
	return -1;
}

static const char *
_verify_type_signature(di_type_t type, void *d, const char *signature) {
	int dtype = _di_type_to_dbus_basic(type);
	if (dbus_type_is_basic(dtype))
		return dtype == *signature ? signature + 1 : NULL;
	if (type == DI_TYPE_ARRAY) {
		if (*signature != 'a')
			return NULL;
		struct di_array *arr = d;
		int step = di_sizeof_type(arr->elem_type);
		auto ret =
		    _verify_type_signature(arr->elem_type, arr->arr, signature + 1);
		for (int i = 1; i < arr->length; i++)
			if (!_verify_type_signature(
			        arr->elem_type, arr->arr + step * i, signature + 1))
				return NULL;
		return ret;
	}
	if (type == DI_TYPE_TUPLE) {
		if (*signature != '(')
			return NULL;
		struct di_tuple *t = d;
		auto curr = signature;
		for (int i = 0; i < t->length; i++) {
			auto next = _verify_type_signature(t->elem_type[i],
			                                   t->tuple[i], curr);
			if (!next)
				return NULL;
			curr = next;
		}
		if (*curr != ')')
			return NULL;
	}
	return NULL;
}

static void free_dbus_signature(struct _dbus_signature sig) {
	for (int i = 0; i < sig.nchild; i++)
		free_dbus_signature(sig.child[i]);
	free(sig.child);
}

static struct _dbus_signature
_type_signature_of_di_value_to_buffer(di_type_t type, void *d, char *buffer) {
	int dtype = _di_type_to_dbus_basic(type);
	if (dbus_type_is_basic(dtype)) {
		*buffer = (char)dtype;
		return (struct _dbus_signature){buffer, 1, 0, NULL};
	}
	if (type == DI_TYPE_ARRAY) {
		*buffer = 'a';
		struct di_array *arr = d;
		auto res = _type_signature_of_di_value_to_buffer(
		    arr->elem_type, arr->arr, buffer + 1);
		auto v = _verify_type_signature(type, d, buffer);
		if (!v) {
			free_dbus_signature(res);
			return (struct _dbus_signature){NULL, -EINVAL, 0, NULL};
		}

		struct _dbus_signature ret;
		ret.current = buffer;
		ret.length = res.length + 1;
		ret.nchild = 1;
		ret.child = tmalloc(struct _dbus_signature, 1);
		ret.child[0] = res;
		return ret;
	}
	if (type == DI_TYPE_TUPLE) {
		char *curr = buffer;
		struct di_tuple *t = d;
		*curr++ = '(';
		struct _dbus_signature ret;
		ret.current = buffer;
		ret.length = 2;
		ret.nchild = t->length;
		ret.child = tmalloc(struct _dbus_signature, t->length);
		for (int i = 0; i < t->length; i++) {
			ret.child[i] = _type_signature_of_di_value_to_buffer(
			    t->elem_type[i], t->tuple[i], curr);
			if (!ret.child[i].current) {
				auto tmp = ret.child[i];
				free_dbus_signature(ret);
				return tmp;
			}
			ret.length += ret.child[i].length;
			curr += ret.child[i].length;
		}
		*curr++ = ')';
		return ret;
	}
	return (struct _dbus_signature){NULL, -EINVAL, 0, NULL};
}

static struct _dbus_signature _type_signature_of_di_value(di_type_t type, void *d) {
	int len = _type_signature_length_of_di_value(type, d);
	if (len < 0)
		return (struct _dbus_signature){NULL, -EINVAL, 0, NULL};
	char *ret = malloc(len + 1);
	auto rc = _type_signature_of_di_value_to_buffer(type, d, ret);
	if (rc.length < 0)
		free(ret);
	return rc;
}

static int _dbus_serialize_with_signature(DBusMessageIter *i, di_type_t type,
                                          void *d, struct _dbus_signature si) {
	int dtype = _di_type_to_dbus_basic(type);
	if (dbus_type_is_basic(dtype)) {
		assert(dtype == *si.current);
		dbus_message_iter_append_basic(i, dtype, d);
		return 0;
	}
	if (type == DI_TYPE_ARRAY) {
		assert(dtype == DBUS_TYPE_ARRAY);
		assert(dtype == *si.current);
		struct di_array *arr = d;
		char atype = _di_type_to_dbus_basic(arr->elem_type);

		assert(si.nchild == 1);
		auto si2 = si.child[0];

		if (dbus_type_is_basic(atype) && atype != DBUS_TYPE_STRING) {
			assert(atype == *si2.current);
			DBusMessageIter i2;
			bool ret = dbus_message_iter_open_container(
			    i, dtype, (char[]){atype, 0}, &i2);
			if (!ret)
				return -ENOMEM;

			// append_fixed_array takes pointer to pointer, makes 0 sense
			dbus_message_iter_append_fixed_array(&i2, atype, &arr->arr,
			                                     arr->length);
			dbus_message_iter_close_container(i, &i2);
			return 0;
		}

		char tmp = si2.current[si.length];
		si2.current[si2.length] = '\0';
		DBusMessageIter i2;
		int step = di_sizeof_type(arr->elem_type);
		bool ret =
		    dbus_message_iter_open_container(i, dtype, si2.current, &i2);
		si2.current[si2.length] = tmp;
		if (!ret)
			return -ENOMEM;
		for (int i = 0; i < arr->length; i++) {
			int ret = _dbus_serialize_with_signature(
			    &i2, arr->elem_type, arr->arr + step * i, si2);
			if (ret < 0)
				return ret;
		}
		dbus_message_iter_close_container(i, &i2);
		return 0;
	}
	if (type == DI_TYPE_TUPLE) {
		assert(dtype == DBUS_TYPE_STRUCT);
		assert(*si.current == '(');
		struct di_tuple *t = d;
		assert(si.length == t->length);
		DBusMessageIter i2;
		bool ret = dbus_message_iter_open_container(i, dtype, NULL, &i2);
		if (!ret)
			return -ENOMEM;
		for (int i = 0; i < t->length; i++) {
			int ret = _dbus_serialize_with_signature(
			    &i2, t->elem_type[i], t->tuple[i], si.child[i]);
			if (ret < 0)
				return ret;
		}
	}
	return -EINVAL;
}

int _dbus_serialize_tuple(DBusMessageIter *it, struct di_tuple t) {
	auto sig = _type_signature_of_di_value(DI_TYPE_TUPLE, &t);
	if (!sig.current)
		return sig.length;
	for (int i = 0; i < t.length; i++) {
		int ret = _dbus_serialize_with_signature(it, t.elem_type[i],
		                                         t.tuple[i], sig.child[i]);
		if (ret < 0)
			return ret;
	}
	free(sig.current);
	free_dbus_signature(sig);
	return 0;
}
