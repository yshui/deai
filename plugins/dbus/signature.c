#include <dbus/dbus.h>

#include <deai/helper.h>
#include <deai/object.h>

#include "common.h"
#include "signature.h"

int di_type_to_dbus_basic(di_type_t type) {
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

struct dbus_signature type_signature_of_di_value(struct di_variant var) {
	int len = type_signature_length_of_di_value(var);
	if (len < 0) {
		return (struct dbus_signature){NULL, -EINVAL, 0, NULL};
	}
	char *ret = malloc(len + 1);
	auto rc = type_signature_of_di_value_to_buffer(var, ret);
	if (rc.length < 0) {
		free(ret);
	}
	ret[len] = '\0';
	return rc;
}

struct dbus_signature parse_dbus_signature_one(const char *signature, const char **rest);
struct dbus_signature
parse_dbus_signaure_sequence(const char *signature, char end_char, const char **rest) {
	const char *curr = signature;
	struct dbus_signature *children = NULL;
	int capacity = 0;
	int length = 0;
	int nchild = 0;
	while (*signature != end_char) {
		const char *next;
		if (nchild == capacity) {
			capacity += capacity + 1;
			children = realloc(children, sizeof(struct dbus_signature) * capacity);
		}
		children[nchild] = parse_dbus_signature_one(signature, &next);
		if (children[nchild].current == NULL) {
			free(children);
			return children[nchild];
		}
		length += children[nchild].length;
		nchild += 1;
		signature = next;
	}
	*rest = signature;
	return (struct dbus_signature){
	    .current = curr,
	    .length = length,
	    .nchild = nchild,
	    .child = realloc(children, sizeof(struct dbus_signature) * nchild)};
}

struct dbus_signature parse_dbus_signature_one(const char *signature, const char **rest) {
	if ((dbus_type_is_valid(*signature) && dbus_type_is_basic(*signature)) ||
	    *signature == 'v') {
		*rest = signature + 1;
		return (struct dbus_signature){
		    .current = signature, .length = 1, .nchild = 0, .child = NULL};
	}

	if (*signature == DBUS_TYPE_ARRAY) {
		auto children = tmalloc(struct dbus_signature, 1);
		children[0] = parse_dbus_signature_one(signature + 1, rest);
		if (children[0].current == NULL) {
			free(children);
			return children[0];
		}
		return (struct dbus_signature){.current = signature,
		                               .length = children[0].length + 1,
		                               .nchild = 1,
		                               .child = children};
	}
	if (*signature == DBUS_STRUCT_BEGIN_CHAR) {
		const char *next;
		auto ret =
		    parse_dbus_signaure_sequence(signature + 1, DBUS_STRUCT_END_CHAR, &next);
		if (ret.current == NULL) {
			return ret;
		}
		assert(*next == DBUS_STRUCT_END_CHAR);
		ret.current = signature;
		ret.length += 2;
		*rest = next + 1;
		return ret;
	}
	return (struct dbus_signature){
	    .current = NULL, .length = -EINVAL, .nchild = 0, .child = NULL};
}

struct dbus_signature parse_dbus_signature(const char *signature) {
	struct dbus_signature ret;
	ret.current = (char *)signature;
	ret.length = strlen(signature);
	const char *rest;
	auto ret2 = parse_dbus_signaure_sequence(ret.current, '\0', &rest);
	if (ret2.current == NULL) {
		return ret2;
	}
	assert(*rest == '\0');
	ret.child = ret2.child;
	ret.nchild = ret2.nchild;
	return ret;
}
