/// This serialize deai values to, and deserialize deai values from dbus values.
/// For most part this is straightforward, only complication is that di_array of
/// di_variants could be either dbus structs (type signature '(...)') or dbus array of
/// variants (type signature 'av')

#include <deai/helper.h>
#include <assert.h>

#include "common.h"
#include "list.h"
#include "sedes.h"
static di_type_t dbus_type_to_di(int type) {
	switch (type) {
	case DBUS_TYPE_BOOLEAN:
		return DI_TYPE_BOOL;
	case DBUS_TYPE_INT16:
	case DBUS_TYPE_INT32:
	case DBUS_TYPE_INT64:
		return DI_TYPE_INT;
	case DBUS_TYPE_UINT16:
	case DBUS_TYPE_UINT32:
	case DBUS_TYPE_UINT64:
		return DI_TYPE_UINT;
	case DBUS_TYPE_DOUBLE:
		return DI_TYPE_FLOAT;
	case DBUS_TYPE_STRING:
		return DI_TYPE_STRING;
	case DBUS_TYPE_UNIX_FD:
		// TODO(yshui)
		return DI_TYPE_INT;
	case DBUS_TYPE_ARRAY:
	case DBUS_TYPE_STRUCT:
		return DI_TYPE_ARRAY;
	default:
		return DI_LAST_TYPE;
	}
}

#define DESERIAL(typeid, type, tgt)                                                      \
	case typeid:                                                                     \
		do {                                                                     \
			type __o;                                                        \
			dbus_message_iter_get_basic(i, &__o);                            \
			retp->tgt = __o;                                                 \
			*otype = di_typeof(retp->tgt);                                   \
		} while (0);                                                             \
		break

static void
dbus_deserialize_basic(DBusMessageIter *i, union di_value *retp, di_type_t *otype, int type) {
	switch (type) {
		DESERIAL(DBUS_TYPE_BOOLEAN, dbus_bool_t, bool_);
		DESERIAL(DBUS_TYPE_INT16, dbus_int16_t, int_);
		DESERIAL(DBUS_TYPE_INT32, dbus_int32_t, int_);
		DESERIAL(DBUS_TYPE_UNIX_FD, dbus_int32_t, int_);
		DESERIAL(DBUS_TYPE_INT64, dbus_int64_t, int_);
		DESERIAL(DBUS_TYPE_UINT16, dbus_uint16_t, uint);
		DESERIAL(DBUS_TYPE_UINT32, dbus_uint32_t, uint);
		DESERIAL(DBUS_TYPE_UINT64, dbus_uint64_t, uint);
		DESERIAL(DBUS_TYPE_DOUBLE, double, float_);
	case DBUS_TYPE_STRING:;
		const char *dbus_string;
		dbus_message_iter_get_basic(i, &dbus_string);
		retp->string = di_string_dup(dbus_string);
		*otype = DI_TYPE_STRING;
		break;
	default:
		assert(false);
	}
}

#undef _DESERIAL

static void dbus_deserialize_one(DBusMessageIter *i, void *retp, di_type_t *otype, int type);

// Deserialize an array. `i' is the iterator, already recursed into the array
// `type' is the array element type
static void
dbus_deserialize_array(DBusMessageIter *i, struct di_array *retp, int type, int length) {
	if (dbus_type_is_fixed(type)) {
		struct di_array ret;
		int length;
		dbus_message_iter_get_fixed_array(i, &ret.arr, &length);
		ret.length = length;
		*retp = ret;
		return;
	}

	struct di_array ret;
	ret.elem_type = dbus_type_to_di(type);

	size_t esize = di_sizeof_type(ret.elem_type);
	ret.length = length;
	if (ret.elem_type >= DI_LAST_TYPE) {
		*retp = DI_ARRAY_INIT;
		return;
	}
	ret.arr = calloc(ret.length, esize);
	for (int x = 0; x < ret.length; x++) {
		di_type_t _;
		dbus_deserialize_one(i, ret.arr + esize * x, &_, type);
		dbus_message_iter_next(i);
	}
	*retp = ret;
}

/// Deserialize a dbus struct to a di_array of di_variants
void _dbus_deserialize_struct(DBusMessageIter *i, void *retp) {
	struct di_tuple t = DI_TUPLE_INIT;
	DBusMessageIter tmpi = *i;
	while (dbus_message_iter_get_arg_type(&tmpi) != DBUS_TYPE_INVALID) {
		dbus_message_iter_next(&tmpi);
		t.length++;
	}

	t.elements = tmalloc(struct di_variant, t.length);
	for (int x = 0; x < t.length; x++) {
		int type = dbus_message_iter_get_arg_type(i);
		t.elements[x].type = dbus_type_to_di(type);

		t.elements[x].value = calloc(1, di_sizeof_type(t.elements[x].type));
		di_type_t rtype;
		dbus_deserialize_one(i, t.elements[x].value, &rtype, type);

		// Dict type can't be discerned from the outer type alone (which
		// would be array).
		// If deserialize_one returns an object and we expect an array,
		// that means it's a dbus dict.
		if (rtype == DI_TYPE_OBJECT && t.elements[x].type == DI_TYPE_ARRAY) {
			t.elements[x].type = rtype;
		}
		assert(rtype == t.elements[x].type);
		dbus_message_iter_next(i);
	}
	*(struct di_tuple *)retp = t;
}

static void dbus_deserialize_dict(DBusMessageIter *i, void *retp, int length) {
	auto o = di_new_object_with_type(struct di_object);
	for (int x = 0; x < length; x++) {
		struct di_tuple t;
		DBusMessageIter i2;
		dbus_message_iter_recurse(i, &i2);
		_dbus_deserialize_struct(&i2, &t);
		assert(t.length == 2);
		assert(t.elements[0].type == DI_TYPE_STRING);
		di_add_member_move(o, t.elements[0].value->string, &t.elements[1].type,
		                   t.elements[1].value);
		di_free_tuple(t);
		dbus_message_iter_next(i);
	}
	*(struct di_object **)retp = o;
}

static void dbus_deserialize_one(DBusMessageIter *i, void *retp, di_type_t *otype, int type) {
	if (dbus_type_is_basic(type)) {
		return dbus_deserialize_basic(i, retp, otype, type);
	}

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
				return dbus_deserialize_dict(
				    &i2, retp, dbus_message_iter_get_element_count(i));
			}
		}

		*otype = DI_TYPE_ARRAY;
		if (type2 == DBUS_TYPE_INVALID) {
			// I think this means the array is empty, dbus doc is a bit vague
			// on this
			*(struct di_array *)retp = DI_ARRAY_INIT;
			return;
		}
		return dbus_deserialize_array(&i2, retp, type2,
		                              dbus_message_iter_get_element_count(i));
	}

	if (type == DBUS_TYPE_STRUCT || type == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter i2;
		dbus_message_iter_recurse(i, &i2);
		*otype = DI_TYPE_TUPLE;
		_dbus_deserialize_struct(&i2, retp);
	}
}

static int di_type_to_dbus_basic(di_type_t type) {
	static_assert(sizeof(int) == 4, "NINT is not INT32");
	static_assert(sizeof(unsigned int) == 4, "NUINT is not UINT32");
	switch (type) {
	case DI_TYPE_BOOL:
		return DBUS_TYPE_BOOLEAN;
	case DI_TYPE_INT:
		return DBUS_TYPE_INT64;
	case DI_TYPE_UINT:
		return DBUS_TYPE_UINT64;
	case DI_TYPE_NINT:
		return DBUS_TYPE_INT32;
	case DI_TYPE_NUINT:
		return DBUS_TYPE_UINT32;
	case DI_TYPE_FLOAT:
		return DBUS_TYPE_DOUBLE;
	case DI_TYPE_STRING:
	case DI_TYPE_STRING_LITERAL:
		return DBUS_TYPE_STRING;
	case DI_TYPE_ARRAY:
		return DBUS_TYPE_ARRAY;
	case DI_TYPE_TUPLE:
		return DBUS_TYPE_STRUCT;
	case DI_TYPE_ANY:
	case DI_LAST_TYPE:
		DI_PANIC("Impossible types appeared in dbus serialization");
	case DI_TYPE_NIL:
	case DI_TYPE_POINTER:
	case DI_TYPE_OBJECT:
	case DI_TYPE_WEAK_OBJECT:
	case DI_TYPE_VARIANT:
	default:
		return DBUS_TYPE_INVALID;
	}
}

struct dbus_signature {
	char *current;
	int length;
	int nchild;
	struct dbus_signature *child;
};

// TODO(yshui) Serialization of arrays is ambiguous. It can be serialized as an array, a
// struct, or a dict in different cases. We need dbus type information from introspection,
// to figure out how to properly serialize the value.
// Same for variants. They can be serialized as variant, or as their inner typep.
// FIXME currently array of variants are always serialized as dbus struct.
static int type_signature_length_of_di_value(di_type_t type, void *d) {
	int dtype = di_type_to_dbus_basic(type);
	if (dbus_type_is_basic(dtype)) {
		return 1;
	}
	if (type == DI_TYPE_ARRAY) {
		struct di_array *arr = d;
		int v0 = type_signature_length_of_di_value(arr->elem_type, arr->arr);
		int step = di_sizeof_type(arr->elem_type);
		for (int i = 1; i < arr->elem_type; i++) {
			int v = type_signature_length_of_di_value(arr->elem_type,
			                                          arr->arr + step * i);
			if (v != v0) {
				return -1;
			}
		}
		return 1 + v0;
	}
	if (type == DI_TYPE_TUPLE) {
		struct di_tuple *t = d;
		int ret = 0;
		for (int i = 0; i < t->length; i++) {
			int tmp = type_signature_length_of_di_value(t->elements[i].type,
			                                            t->elements[i].value);
			if (tmp < 0) {
				return tmp;
			}
			ret += tmp;
		}
		return ret + 2;
	}
	return -1;
}

static const char *verify_type_signature(di_type_t type, void *d, const char *signature) {
	int dtype = di_type_to_dbus_basic(type);
	if (dbus_type_is_basic(dtype)) {
		return dtype == *signature ? signature + 1 : NULL;
	}
	struct di_array *arr = d;
	if (type == DI_TYPE_ARRAY && arr->elem_type != DI_TYPE_VARIANT) {
		if (*signature != 'a') {
			return NULL;
		}
		int step = di_sizeof_type(arr->elem_type);
		auto ret = verify_type_signature(arr->elem_type, arr->arr, signature + 1);
		for (int i = 1; i < arr->length; i++) {
			if (!verify_type_signature(arr->elem_type, arr->arr + step * i,
			                           signature + 1)) {
				return NULL;
			}
		}
		return ret;
	}
	if (type == DI_TYPE_ARRAY && arr->elem_type == DI_TYPE_VARIANT) {
		struct di_variant *vars = arr->arr;
		if (*signature != '(') {
			return NULL;
		}
		auto curr = signature;
		for (int i = 0; i < arr->length; i++) {
			auto next = verify_type_signature(vars[i].type, vars[i].value, curr);
			if (!next) {
				return NULL;
			}
			curr = next;
		}
		if (*curr != ')') {
			return NULL;
		}
	}
	return NULL;
}

static void free_dbus_signature(struct dbus_signature sig) {
	for (int i = 0; i < sig.nchild; i++) {
		free_dbus_signature(sig.child[i]);
	}
	free(sig.child);
}

static struct dbus_signature
type_signature_of_di_value_to_buffer(di_type_t type, void *d, char *buffer) {
	int dtype = di_type_to_dbus_basic(type);
	if (dbus_type_is_basic(dtype)) {
		*buffer = (char)dtype;
		return (struct dbus_signature){buffer, 1, 0, NULL};
	}
	if (type == DI_TYPE_ARRAY) {
		*buffer = 'a';
		struct di_array *arr = d;
		auto res = type_signature_of_di_value_to_buffer(arr->elem_type, arr->arr,
		                                                buffer + 1);
		auto v = verify_type_signature(type, d, buffer);
		if (!v) {
			free_dbus_signature(res);
			return (struct dbus_signature){NULL, -EINVAL, 0, NULL};
		}

		struct dbus_signature ret;
		ret.current = buffer;
		ret.length = res.length + 1;
		ret.nchild = 1;
		ret.child = tmalloc(struct dbus_signature, 1);
		ret.child[0] = res;
		return ret;
	}
	if (type == DI_TYPE_TUPLE) {
		char *curr = buffer;
		*curr++ = '(';
		struct di_tuple *t = d;
		struct dbus_signature ret;
		ret.current = buffer;
		ret.length = 2;
		ret.nchild = t->length;
		ret.child = tmalloc(struct dbus_signature, t->length);
		for (int i = 0; i < t->length; i++) {
			ret.child[i] = type_signature_of_di_value_to_buffer(
			    t->elements[i].type, t->elements[i].value, curr);
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
	return (struct dbus_signature){NULL, -EINVAL, 0, NULL};
}

static struct dbus_signature type_signature_of_di_value(di_type_t type, void *d) {
	int len = type_signature_length_of_di_value(type, d);
	if (len < 0) {
		return (struct dbus_signature){NULL, -EINVAL, 0, NULL};
	}
	char *ret = malloc(len + 1);
	auto rc = type_signature_of_di_value_to_buffer(type, d, ret);
	if (rc.length < 0) {
		free(ret);
	}
	return rc;
}

static int dbus_serialize_with_signature(DBusMessageIter *i, struct di_variant var,
                                         struct dbus_signature si) {
	int dtype = di_type_to_dbus_basic(var.type);
	if (dbus_type_is_basic(dtype)) {
		assert(dtype == *si.current);
		if (var.type == DI_TYPE_STRING) {
			char *tmp = di_string_to_chars_alloc(var.value->string);
			dbus_message_iter_append_basic(i, dtype, &tmp);
			free(tmp);
		} else {
			dbus_message_iter_append_basic(i, dtype, var.value);
		}
		return 0;
	}
	struct di_array *arr = &var.value->array;
	if (var.type == DI_TYPE_ARRAY && arr->elem_type != DI_TYPE_VARIANT) {
		assert(dtype == DBUS_TYPE_ARRAY);
		assert(dtype == *si.current);
		int atype = di_type_to_dbus_basic(arr->elem_type);

		assert(si.nchild == 1);
		auto si2 = si.child[0];

		if (dbus_type_is_basic(atype) && atype != DBUS_TYPE_STRING) {
			assert(atype == *si2.current);
			DBusMessageIter i2;
			bool ret = dbus_message_iter_open_container(
			    i, dtype, (char[]){(char)atype, 0}, &i2);
			if (!ret) {
				return -ENOMEM;
			}

			// append_fixed_array takes pointer to pointer, makes 0 sense
			dbus_message_iter_append_fixed_array(&i2, atype, &arr->arr, arr->length);
			dbus_message_iter_close_container(i, &i2);
			return 0;
		}

		char tmp = si2.current[si.length];
		si2.current[si2.length] = '\0';
		DBusMessageIter i2;
		int step = di_sizeof_type(arr->elem_type);
		bool ret = dbus_message_iter_open_container(i, dtype, si2.current, &i2);
		si2.current[si2.length] = tmp;
		if (!ret) {
			return -ENOMEM;
		}
		for (int i = 0; i < arr->length; i++) {
			int ret = dbus_serialize_with_signature(
			    &i2, (struct di_variant){arr->arr + step * i, arr->elem_type}, si2);
			if (ret < 0) {
				return ret;
			}
		}
		dbus_message_iter_close_container(i, &i2);
		return 0;
	}
	if (var.type == DI_TYPE_ARRAY && arr->elem_type == DI_TYPE_VARIANT) {
		assert(dtype == DBUS_TYPE_STRUCT);
		assert(*si.current == '(');
		assert(si.length == arr->length);
		struct di_variant *vars = arr->arr;
		DBusMessageIter i2;
		bool ret = dbus_message_iter_open_container(i, dtype, NULL, &i2);
		if (!ret) {
			return -ENOMEM;
		}
		for (int i = 0; i < arr->length; i++) {
			int ret = dbus_serialize_with_signature(&i2, vars[i], si.child[i]);
			if (ret < 0) {
				return ret;
			}
		}
	}
	return -EINVAL;
}

int _dbus_serialize_struct(DBusMessageIter *it, struct di_tuple t) {
	auto sig = type_signature_of_di_value(DI_TYPE_TUPLE, &t);
	if (!sig.current) {
		return sig.length;
	}
	for (int i = 0; i < t.length; i++) {
		int ret = dbus_serialize_with_signature(it, t.elements[i], sig.child[i]);
		if (ret < 0) {
			return ret;
		}
	}
	free(sig.current);
	free_dbus_signature(sig);
	return 0;
}
