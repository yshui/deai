#include <deai/deai.h>
#include <assert.h>

#include "common.h"
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
		assert(rtype == t.elem_type[x]);
		dbus_message_iter_next(i);
	}
	*(struct di_tuple *)retp = t;
}

static void _dbus_deserialize_dict(DBusMessageIter *i, void *retp) {
	auto o = di_new_object_with_type(struct di_object);
	int length = dbus_message_iter_get_element_count(i);
	for (int x = 0; x < length; x++) {
		struct di_tuple t;
		DBusMessageIter i2;
		dbus_message_iter_recurse(i, &i2);
		_dbus_deserialize_tuple(&i2, &t);
		assert(t.length == 2);
		assert(t.elem_type[0] == DI_TYPE_STRING);
		di_add_ref_member(o, *(const char **)t.tuple[0], false,
		                  t.elem_type[1], t.tuple[1]);
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
				return _dbus_deserialize_dict(&i2, retp);
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
