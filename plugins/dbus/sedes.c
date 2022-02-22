/// This serialize deai values to, and deserialize deai values from dbus values.
/// For most part this is straightforward, only complication is that di_array of
/// di_variants could be either dbus structs (type signature '(...)') or dbus array of
/// variants (type signature 'av')

#include <deai/helper.h>
#include <assert.h>

#include "common.h"
#include "list.h"
#include "sedes.h"
#include "utils.h"
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
	case DBUS_TYPE_OBJECT_PATH:
		return DI_TYPE_STRING;
	case DBUS_TYPE_UNIX_FD:
		// TODO(yshui)
		return DI_TYPE_INT;
	case DBUS_TYPE_ARRAY:
		return DI_TYPE_ARRAY;
	case DBUS_TYPE_STRUCT:
		return DI_TYPE_TUPLE;
	case DBUS_TYPE_VARIANT:
		return DI_TYPE_VARIANT;
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
	case DBUS_TYPE_STRING:
	case DBUS_TYPE_OBJECT_PATH:;
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

/// Deserialize a dbus struct to a di_tuple
void dbus_deserialize_struct(DBusMessageIter *i, void *retp) {
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
		dbus_deserialize_struct(&i2, &t);
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

	if (type == DBUS_TYPE_VARIANT) {
		DBusMessageIter i2;
		struct di_variant *v = retp;
		dbus_message_iter_recurse(i, &i2);
		int type2 = dbus_message_iter_get_arg_type(&i2);
		di_type_t di_type = DI_LAST_TYPE;
		v->type = dbus_type_to_di(type2);
		v->value = calloc(1, di_sizeof_type(v->type));
		dbus_deserialize_one(&i2, v->value, &di_type, type2);
		assert(di_type == v->type);
		*otype = DI_TYPE_VARIANT;
		return;
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
		dbus_deserialize_struct(&i2, retp);
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
// Same for variants. They can be serialized as variant, or as their inner types.
static int type_signature_length_of_di_value(struct di_variant var) {
	int dtype = di_type_to_dbus_basic(var.type);
	if (dbus_type_is_basic(dtype)) {
		return 1;
	}
	if (var.type == DI_TYPE_ARRAY) {
		struct di_array arr = var.value->array;
		int v0 = type_signature_length_of_di_value(di_array_index(arr, 0));
		for (int i = 1; i < arr.elem_type; i++) {
			int v = type_signature_length_of_di_value(di_array_index(arr, 0));
			if (v != v0) {
				return -1;
			}
		}
		return 1 + v0;
	}
	if (var.type == DI_TYPE_TUPLE) {
		struct di_tuple t = var.value->tuple;
		int ret = 0;
		for (int i = 0; i < t.length; i++) {
			int tmp = type_signature_length_of_di_value(t.elements[i]);
			if (tmp < 0) {
				return tmp;
			}
			ret += tmp;
		}
		return ret + 2;
	}
	if (var.type == DI_TYPE_VARIANT) {
		return type_signature_length_of_di_value(var.value->variant);
	}
	return -1;
}

/// Whether deai type `type` can be converted to `dbus_type`
static bool is_basic_type_compatible(di_type_t type, int dbus_type) {
	switch (dbus_type) {
	case DBUS_TYPE_BOOLEAN:
		return type == DI_TYPE_BOOL;
	case DBUS_TYPE_INT16:
	case DBUS_TYPE_INT32:
	case DBUS_TYPE_INT64:
	case DBUS_TYPE_UINT16:
	case DBUS_TYPE_UINT32:
	case DBUS_TYPE_UINT64:
		return type == DI_TYPE_UINT || type == DI_TYPE_INT ||
		       type == DI_TYPE_NINT || type == DI_TYPE_NUINT;
	case DBUS_TYPE_DOUBLE:
		return type == DI_TYPE_FLOAT;
	case DBUS_TYPE_STRING:
	case DBUS_TYPE_OBJECT_PATH:
		return type == DI_TYPE_STRING;
	case DBUS_TYPE_UNIX_FD:
		// TODO(yshui)
		return false;
	case DBUS_TYPE_ARRAY:
	case DBUS_TYPE_STRUCT:
	case DBUS_TYPE_VARIANT:
		assert(false);
	default:
		return false;
	}
}

static bool dbus_serialize_integer(DBusMessageIter *i, struct di_variant var, int dbus_type) {
	int8_t dbus_bits;
	bool dbus_unsigned;
	switch (dbus_type) {
	case DBUS_TYPE_INT16:
		dbus_unsigned = false;
		dbus_bits = 16;
		break;
	case DBUS_TYPE_INT32:
		dbus_unsigned = false;
		dbus_bits = 32;
		break;
	case DBUS_TYPE_INT64:
		dbus_unsigned = false;
		dbus_bits = 64;
		break;
	case DBUS_TYPE_UINT16:
		dbus_unsigned = false;
		dbus_bits = 16;
		break;
	case DBUS_TYPE_UINT32:
		dbus_unsigned = false;
		dbus_bits = 32;
		break;
	case DBUS_TYPE_UINT64:
		dbus_unsigned = false;
		dbus_bits = 64;
		break;
	default:
		return false;
	}
	int di_unsigned = is_unsigned(var.type);
	if (di_unsigned == 2) {
		return false;
	}
	int8_t di_bits = di_sizeof_type(var.type) * 8;
	char buf[sizeof(intmax_t)];
	if (!integer_conversion_impl(di_bits, var.value, dbus_bits, buf, di_unsigned == 1,
	                             dbus_unsigned)) {
		return false;
	}
	return dbus_message_iter_append_basic(i, dbus_type, buf);
}

static bool
dbus_serialize_basic_with_type(DBusMessageIter *i, struct di_variant var, int dbus_type) {
	switch (dbus_type) {
	case DBUS_TYPE_BOOLEAN:
		if (var.type == DI_TYPE_BOOL) {
			return dbus_message_iter_append_basic(i, dbus_type, var.value);
		}
		return false;
	case DBUS_TYPE_INT16:
	case DBUS_TYPE_INT32:
	case DBUS_TYPE_INT64:
	case DBUS_TYPE_UINT16:
	case DBUS_TYPE_UINT32:
	case DBUS_TYPE_UINT64:
		return dbus_serialize_integer(i, var, dbus_type);
	case DBUS_TYPE_DOUBLE:
		if (var.type == DI_TYPE_FLOAT) {
			return dbus_message_iter_append_basic(i, dbus_type, var.value);
		}
		return false;
	case DBUS_TYPE_STRING:
	case DBUS_TYPE_OBJECT_PATH:;
		bool ret = false;
		if (var.type == DI_TYPE_STRING) {
			char *tmp = di_string_to_chars_alloc(var.value->string);
			ret = dbus_message_iter_append_basic(i, dbus_type, &tmp);
			free(tmp);
		}
		return ret;
	case DBUS_TYPE_UNIX_FD:
		// TODO(yshui)
		return false;
	case DBUS_TYPE_ARRAY:
	case DBUS_TYPE_STRUCT:
	case DBUS_TYPE_VARIANT:
		assert(false);
	default:
		return false;
	}
}

/// Verify a deai value `d` of type `type` against a dbus type signature. Some type
/// conversion will be performed. i.e. conversion between integer types, string to/from
/// object path, deai array/tuple vs dbus struct/array/dict, deai variant vs dbus variant
/// or plain dbus type
///
/// Returns the rest of the dbus signature not matched with `d`.
static const char *verify_type_signature(struct di_variant var, const char *signature) {
	int dtype = di_type_to_dbus_basic(var.type);
	if (dbus_type_is_basic(dtype)) {
		return is_basic_type_compatible(var.type, *signature) ? signature + 1 : NULL;
	}
	// If our target is a variant, stop here since anything can become a variant
	if (*signature == DBUS_TYPE_VARIANT) {
		return signature + 1;
	}

	if (var.type == DI_TYPE_VARIANT) {
		// We aren't expecting a variant, so unwrap it.
		return verify_type_signature(var.value->variant, signature);
	}

	if (*signature == DBUS_TYPE_ARRAY) {
		if (var.type != DI_TYPE_ARRAY) {
			// In theory, a tuple of all same type can be an array, but we
			// don't do it.
			return NULL;
		}
		struct di_array arr = var.value->array;
		auto ret = verify_type_signature(di_array_index(arr, 0), signature + 1);
		if (arr.elem_type == DI_TYPE_ARRAY || arr.elem_type == DI_TYPE_TUPLE ||
		    arr.elem_type == DI_TYPE_VARIANT) {
			// These types can have different internal structures, but dbus
			// require them to all be the same.
			for (int i = 1; i < arr.length; i++) {
				auto ret2 = verify_type_signature(di_array_index(arr, i),
				                                  signature + 1);
				if (ret2 == NULL || ret2 != ret) {
					return NULL;
				}
			}
		}
		return ret;
	}

	if (*signature == DBUS_STRUCT_BEGIN_CHAR) {
		if (var.type == DI_TYPE_ARRAY) {
			struct di_array arr = var.value->array;
			auto curr = signature + 1;
			for (int i = 0; i < arr.length; i++) {
				auto next = verify_type_signature(di_array_index(arr, i), curr);
				if (!next) {
					return NULL;
				}
				curr = next;
			}
			if (*curr != ')') {
				return NULL;
			}
			return curr + 1;
		}
		if (var.type == DI_TYPE_TUPLE) {
			auto curr = signature + 1;
			struct di_tuple t = var.value->tuple;
			for (int i = 0; i < t.length; i++) {
				auto next = verify_type_signature(t.elements[i], curr);
				if (!next) {
					return NULL;
				}
				curr = next;
			}
			if (*curr != ')') {
				return NULL;
			}
			return curr + 1;
		}
	}
	// TODO: handle dict
	return NULL;
}

static void free_dbus_signature(struct dbus_signature sig) {
	for (int i = 0; i < sig.nchild; i++) {
		free_dbus_signature(sig.child[i]);
	}
	free(sig.child);
}

static struct dbus_signature
type_signature_of_di_value_to_buffer(struct di_variant var, char *buffer) {
	int dtype = di_type_to_dbus_basic(var.type);
	if (dbus_type_is_basic(dtype)) {
		*buffer = (char)dtype;
		return (struct dbus_signature){buffer, 1, 0, NULL};
	}
	if (var.type == DI_TYPE_ARRAY) {
		*buffer = 'a';
		struct di_array arr = var.value->array;
		auto res =
		    type_signature_of_di_value_to_buffer(di_array_index(arr, 0), buffer + 1);
		auto v = verify_type_signature(var, buffer);
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
	if (var.type == DI_TYPE_TUPLE) {
		char *curr = buffer;
		*curr++ = '(';
		struct di_tuple t = var.value->tuple;
		struct dbus_signature ret;
		ret.current = buffer;
		ret.length = 2;
		ret.nchild = t.length;
		ret.child = tmalloc(struct dbus_signature, t.length);
		for (int i = 0; i < t.length; i++) {
			ret.child[i] =
			    type_signature_of_di_value_to_buffer(t.elements[i], curr);
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
	if (var.type == DI_TYPE_VARIANT) {
		return type_signature_of_di_value_to_buffer(var.value->variant, buffer);
	}
	return (struct dbus_signature){NULL, -EINVAL, 0, NULL};
}

static struct dbus_signature type_signature_of_di_value(struct di_variant var) {
	int len = type_signature_length_of_di_value(var);
	if (len < 0) {
		return (struct dbus_signature){NULL, -EINVAL, 0, NULL};
	}
	char *ret = malloc(len + 1);
	auto rc = type_signature_of_di_value_to_buffer(var, ret);
	if (rc.length < 0) {
		free(ret);
	}
	return rc;
}

static int dbus_serialize_with_signature(DBusMessageIter *i, struct di_variant var,
                                         struct dbus_signature si) {
	int dtype = di_type_to_dbus_basic(var.type);
	if (dbus_type_is_basic(dtype)) {
		if (!dbus_serialize_basic_with_type(i, var, *si.current)) {
			return -EINVAL;
		}
		return 0;
	}
	if (*si.current == DBUS_TYPE_VARIANT) {
		auto inner = type_signature_of_di_value(var);
		DBusMessageIter i2;
		dbus_message_iter_open_container(i, DBUS_TYPE_VARIANT, inner.current, &i2);
		dbus_serialize_with_signature(&i2, var, inner);
		return dbus_message_iter_close_container(i, &i2) ? 0 : -ENOMEM;
	}

	if (var.type == DI_TYPE_VARIANT) {
		// We aren't expecting a variant, so unwrap it.
		if (!dbus_serialize_with_signature(i, var.value->variant, si)) {
			return -ENOMEM;
		}
		return 0;
	}
	if (*si.current == DBUS_TYPE_ARRAY) {
		if (var.type != DI_TYPE_ARRAY) {
			return -EINVAL;
		}
		struct di_array arr = var.value->array;
		int atype = di_type_to_dbus_basic(arr.elem_type);

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
			if (!dbus_message_iter_append_fixed_array(&i2, atype, &arr.arr,
			                                          arr.length)) {
				return -ENOMEM;
			}
			return dbus_message_iter_close_container(i, &i2) ? 0 : -ENOMEM;
		}

		char *tmp = strndup(si2.current, si2.length);
		DBusMessageIter i2;
		int step = di_sizeof_type(arr.elem_type);
		bool ret = dbus_message_iter_open_container(i, dtype, tmp, &i2);
		free(tmp);
		if (!ret) {
			return -ENOMEM;
		}
		for (int i = 0; i < arr.length; i++) {
			int ret = dbus_serialize_with_signature(
			    &i2, (struct di_variant){arr.arr + step * i, arr.elem_type}, si2);
			if (ret < 0) {
				return ret;
			}
		}
		return dbus_message_iter_close_container(i, &i2) ? 0 : -ENOMEM;
	}
	if (*si.current == DBUS_STRUCT_BEGIN_CHAR) {
		if (var.type == DI_TYPE_ARRAY) {
			struct di_array arr = var.value->array;
			if (si.length != arr.length) {
				return -EINVAL;
			}
			DBusMessageIter i2;
			bool ret = dbus_message_iter_open_container(i, dtype, NULL, &i2);
			if (!ret) {
				return -ENOMEM;
			}
			for (int i = 0; i < arr.length; i++) {
				int ret = dbus_serialize_with_signature(
				    &i2, di_array_index(arr, i), si.child[i]);
				if (ret < 0) {
					return ret;
				}
			}
			return dbus_message_iter_close_container(i, &i2) ? 0 : -ENOMEM;
		}
		if (var.type == DI_TYPE_TUPLE) {
			struct di_tuple t = var.value->tuple;
			if (si.length != t.length) {
				return -EINVAL;
			}
			DBusMessageIter i2;
			bool ret = dbus_message_iter_open_container(i, dtype, NULL, &i2);
			if (!ret) {
				return -ENOMEM;
			}
			for (int i = 0; i < t.length; i++) {
				int ret = dbus_serialize_with_signature(
				    &i2, t.elements[i], si.child[i]);
				if (ret < 0) {
					return ret;
				}
			}
			return dbus_message_iter_close_container(i, &i2) ? 0 : -ENOMEM;
		}
	}
	return -EINVAL;
}

int dbus_serialize_struct(DBusMessageIter *it, struct di_tuple t) {
	auto var = di_variant_of(t);
	auto sig = type_signature_of_di_value(var);
	if (!sig.current) {
		// error code is return via .length
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
