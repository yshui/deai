/// This serialize deai values to, and deserialize deai values from dbus values.
/// For most part this is straightforward, only complication is that di_array of
/// di_variants could be either dbus structs (type signature '(...)') or dbus array of
/// variants (type signature 'av')

#include <deai/helper.h>
#include <assert.h>

#include "common.h"
#include "list.h"
#include "sedes.h"
#include "signature.h"
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

static int dbus_serialize_with_signature(DBusMessageIter *i, struct di_variant var,
                                         struct dbus_signature si) {
	if (dbus_type_is_basic(*si.current)) {
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
		auto ret = dbus_message_iter_close_container(i, &i2) ? 0 : -ENOMEM;
		free((char *)inner.current);
		free_dbus_signature(inner);
		return ret;
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
		DBusMessageIter i2;

		if (dbus_type_is_basic(atype) && atype != DBUS_TYPE_STRING &&
		    atype == *si2.current) {
			// Basic data type and no conversion needed
			bool ret = dbus_message_iter_open_container(
			    i, DBUS_TYPE_ARRAY, (char[]){(char)atype, 0}, &i2);
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
		int step = di_sizeof_type(arr.elem_type);
		bool ret = dbus_message_iter_open_container(i, DBUS_TYPE_ARRAY, tmp, &i2);
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
		DBusMessageIter i2;
		bool ret = dbus_message_iter_open_container(i, DBUS_TYPE_STRUCT, NULL, &i2);
		if (!ret) {
			return -ENOMEM;
		}
		if (var.type == DI_TYPE_ARRAY) {
			struct di_array arr = var.value->array;
			if (si.nchild != arr.length) {
				return -EINVAL;
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
			if (si.nchild != t.length) {
				return -EINVAL;
			}
			for (int i = 0; i < t.length; i++) {
				int ret = dbus_serialize_with_signature(
				    &i2, t.elements[i], si.child[i]);
				if (ret < 0) {
					return ret;
				}
			}
		}
		return dbus_message_iter_close_container(i, &i2) ? 0 : -ENOMEM;
	}
	return -EINVAL;
}

int dbus_serialize_struct(DBusMessageIter *it, struct di_tuple t, const char *signature) {
	auto var = di_variant_of(t);
	struct dbus_signature sig;
	int ret = 0;
	if (!signature) {
		sig = type_signature_of_di_value(var);
	} else {
		sig = parse_dbus_signature(signature);
	}
	if (!sig.current) {
		// error code is return via .length
		return sig.length;
	}
	for (int i = 0; i < t.length; i++) {
		ret = dbus_serialize_with_signature(it, t.elements[i], sig.child[i]);
		if (ret < 0) {
			break;
		}
	}

	if (!signature) {
		free((char *)sig.current);
	}
	free_dbus_signature(sig);
	free(var.value);
	return ret;
}
