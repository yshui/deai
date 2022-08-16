// SPDX-License-Identifier: MPL-2.0

/* Copyright (c) 2022, Yuxuan Shui <yshuiv7@gmail.com> */
#pragma once
#include "compiler.h"
#include "helper.h"
#include "object.h"

#define to_max(ui)                                                                       \
	static inline intmax_t to_##ui##max(int8_t input_bits, const void *input) {      \
		switch (input_bits) {                                                    \
		case 8:                                                                  \
			return *(const ui##8_t *)input;                                  \
		case 16:                                                                 \
			return *(const ui##16_t *)input;                                 \
		case 32:                                                                 \
			return *(const ui##32_t *)input;                                 \
		case 64:                                                                 \
			return *(const ui##64_t *)input;                                 \
		default:                                                                 \
			assert(false);                                                   \
		}                                                                        \
	}
#define int_is_unsigned() false
#define uint_is_unsigned() true
#define from_max(target_bits, ui, uo)                                                    \
	static inline bool from_##ui##max_t_to_##uo##target_bits##_t(ui##max_t input,    \
	                                                             void *output) {     \
		uo##target_bits##_t tmp = input;                                         \
		ui##max_t tmp2 = tmp;                                                    \
		if (tmp2 != input) {                                                     \
			return false;                                                    \
		}                                                                        \
		if (CONCAT(ui, _is_unsigned)() && tmp < 0) {                             \
			return false;                                                    \
		}                                                                        \
		if (CONCAT(uo, _is_unsigned)() && input < 0) {                           \
			return false;                                                    \
		}                                                                        \
		*(uo##target_bits##_t *)output = tmp;                                    \
		return true;                                                             \
	}

#define from_max_to_any(ui, uo)                                                          \
	static inline bool from_##ui##max_t_to_##uo##_any(                               \
	    ui##max_t input, int8_t output_bits, void *output) {                         \
		switch (output_bits) {                                                   \
		case 8:                                                                  \
			return from_##ui##max_t_to_##uo##8_t(input, output);             \
		case 16:                                                                 \
			return from_##ui##max_t_to_##uo##16_t(input, output);            \
		case 32:                                                                 \
			return from_##ui##max_t_to_##uo##32_t(input, output);            \
		case 64:                                                                 \
			return from_##ui##max_t_to_##uo##64_t(input, output);            \
		default:                                                                 \
			assert(false);                                                   \
		}                                                                        \
	}

to_max(int);
to_max(uint);

#define from_maxii(x) from_max(x, int, int)
#define from_maxiu(x) from_max(x, int, uint)
#define from_maxui(x) from_max(x, uint, int)
#define from_maxuu(x) from_max(x, uint, uint)
LIST_APPLY(from_maxii, SEP_NONE, 8, 16, 32, 64);
LIST_APPLY(from_maxui, SEP_NONE, 8, 16, 32, 64);
LIST_APPLY(from_maxiu, SEP_NONE, 8, 16, 32, 64);
LIST_APPLY(from_maxuu, SEP_NONE, 8, 16, 32, 64);

from_max_to_any(int, int);
from_max_to_any(int, uint);
from_max_to_any(uint, int);
from_max_to_any(uint, uint);

static inline bool
integer_conversion_impl(int8_t input_bits, const void *input, int8_t output_bits,
                        void *output, bool input_unsigned, bool output_unsigned) {
	if (input_unsigned) {
		uintmax_t tmp = to_uintmax(input_bits, input);
		if (output_unsigned) {
			return from_uintmax_t_to_uint_any(tmp, output_bits, output);
		} else {
			return from_uintmax_t_to_int_any(tmp, output_bits, output);
		}
	} else {
		intmax_t tmp = to_intmax(input_bits, input);
		if (output_unsigned) {
			return from_intmax_t_to_uint_any(tmp, output_bits, output);
		} else {
			return from_intmax_t_to_int_any(tmp, output_bits, output);
		}
	}
	unreachable();
}
/// Whether di_type is an unsigned integer type. Returns 0 if signed, 1 if unsigned, 2 if
/// not an integer type.
static inline int is_unsigned(di_type type) {
	switch (type) {
	case DI_TYPE_INT:
	case DI_TYPE_NINT:
		return 0;
	case DI_TYPE_UINT:
	case DI_TYPE_NUINT:
		return 1;
	case DI_TYPE_ANY:
	case DI_TYPE_NIL:
	case DI_TYPE_EMPTY_OBJECT:
	case DI_TYPE_FLOAT:
	case DI_TYPE_BOOL:
	case DI_TYPE_ARRAY:
	case DI_TYPE_TUPLE:
	case DI_TYPE_VARIANT:
	case DI_TYPE_OBJECT:
	case DI_TYPE_WEAK_OBJECT:
	case DI_TYPE_STRING:
	case DI_TYPE_STRING_LITERAL:
	case DI_TYPE_POINTER:
	case DI_LAST_TYPE:
	default:
		return 2;
	}
}
static inline int integer_conversion(di_type inty, const di_value *restrict inp,
                                     di_type outty, di_value *restrict outp) {
	int input_unsigned = is_unsigned(inty), output_unsigned = is_unsigned(outty);
	int8_t input_bits = di_sizeof_type(inty) * 8, output_bits = di_sizeof_type(outty) * 8;
	if (input_unsigned == 2 || output_unsigned == 2) {
		return -EINVAL;
	}
	if (inty == outty) {
		memcpy(outp, inp, di_sizeof_type(outty));
		return 0;
	}
	if (!integer_conversion_impl(input_bits, inp, output_bits, outp,
	                             input_unsigned == 1, output_unsigned == 1)) {
		return -ERANGE;
	}
	return 0;
}

static inline bool is_integer(di_type t) {
	return t == DI_TYPE_INT || t == DI_TYPE_NINT || t == DI_TYPE_UINT || t == DI_TYPE_NUINT;
}

/// Convert value `inp` of type `inty` to type `outty`. The conversion operates in 2 mode:
///
///   If `inp`'s value is owned, it will be destructed during conversion, the ownership
///   will be transferred to `outp`. There is no need to free `inp` afterwards, but `outp`
///   is expected to be freed by the caller.
///
///   If `inp`'s value is not owned, the conversion will not touch `inp`, and `outp`
///   will borrow the value in `inp`. `outp` must not be freed afterwards.
///
/// Returns 0 on success, -EINVAL if the conversion is not possible, -ERANGE if source is
/// a integer type and its value is out of range of the destination type. If the
/// conversion fails, `outp` is left untouched.
///
/// @param[in] borrowing Whether the `inp` value is owned or borrowed. Some conversion can
//                       not be performed if the caller owns the value, as that would
//                       cause memory leakage. If `inp` is borrowed, `outp` must also be
//                       borrowed downstream as well.
static inline int unused di_type_conversion(di_type inty, di_value *inp, di_type outty,
                                            di_value *outp, bool borrowing) {
	if (inty == outty) {
		memcpy(outp, inp, di_sizeof_type(inty));
		return 0;
	}

	if ((inty == DI_TYPE_OBJECT && outty == DI_TYPE_EMPTY_OBJECT) ||
	    (inty == DI_TYPE_EMPTY_OBJECT && outty == DI_TYPE_OBJECT)) {
		// Note we don't check if an object is actually empty, because of getters,
		// etc., it's impossible
		outp->object = inp->object;
		return 0;
	}

	if (inty == DI_TYPE_EMPTY_OBJECT && outty == DI_TYPE_ARRAY) {
		if (!borrowing) {
			di_unref_object(inp->object);
		}
		outp->array = DI_ARRAY_INIT;
		return 0;
	}

	if (inty == DI_TYPE_EMPTY_OBJECT && outty == DI_TYPE_TUPLE) {
		if (!borrowing) {
			di_unref_object(inp->object);
		}
		outp->tuple = DI_TUPLE_INIT;
		return 0;
	}

	if (inty == DI_TYPE_NIL) {
		switch (outty) {
		case DI_TYPE_WEAK_OBJECT:
			outp->weak_object = (void *)&dead_weak_ref;
			return 0;
		case DI_TYPE_POINTER:
			outp->pointer = NULL;
			return 0;
		case DI_TYPE_ARRAY:
			outp->array = DI_ARRAY_INIT;
			return 0;
		case DI_TYPE_TUPLE:
			outp->tuple = DI_TUPLE_INIT;
			return 0;
		case DI_TYPE_ANY:
		case DI_LAST_TYPE:
			assert(false && "Impossible types appeared in arguments");
			return -EINVAL;
		case DI_TYPE_NIL:
		case DI_TYPE_VARIANT:
			unreachable();
		case DI_TYPE_EMPTY_OBJECT:
		case DI_TYPE_OBJECT:
		case DI_TYPE_FLOAT:
		case DI_TYPE_BOOL:
		case DI_TYPE_INT:
		case DI_TYPE_UINT:
		case DI_TYPE_NINT:
		case DI_TYPE_NUINT:
		case DI_TYPE_STRING:
		case DI_TYPE_STRING_LITERAL:
			break;
		}
	}

	if (outty == DI_TYPE_VARIANT) {
		outp->variant.type = inty;
		outp->variant.value = inp;
		return 0;
	}

	if (inty == DI_TYPE_STRING_LITERAL && outty == DI_TYPE_STRING) {
		if (borrowing) {
			outp->string = (di_string){
			    .data = inp->string_literal,
			    .length = strlen(inp->string_literal),
			};
		} else {
			// If downstream expect an owned string, they will try to
			// free it, so we have to cloned the string literal
			outp->string = (di_string){
			    .data = strdup(inp->string_literal),
			    .length = strlen(inp->string_literal),
			};
		}
		return 0;
	}

	if (outty == DI_TYPE_NIL) {
		if (!borrowing) {
			di_free_value(inty, inp);
		}
		return 0;
	}

	if (inty == DI_TYPE_VARIANT) {
		int ret = di_type_conversion(inp->variant.type, inp->variant.value, outty,
		                             outp, borrowing);
		if (ret != 0) {
			return ret;
		}
		if (!borrowing) {
			free(inp->variant.value);
		}
		return 0;
	}

	if (is_integer(inty)) {
		if (is_integer(outty)) {
			return integer_conversion(inty, inp, outty, outp);
		}
		if (outty == DI_TYPE_FLOAT) {
#define convert_case(srcfield)                                                           \
	case di_typeof(((di_value *)0)->srcfield):                                       \
		outp->float_ = (double)inp->srcfield;                                    \
		break;
			switch (inty) {
				convert_case(nuint);
				convert_case(nint);
				convert_case(uint);
				convert_case(int_);
			case DI_TYPE_ANY:
			case DI_TYPE_NIL:
			case DI_TYPE_FLOAT:
			case DI_TYPE_BOOL:
			case DI_TYPE_ARRAY:
			case DI_TYPE_TUPLE:
			case DI_TYPE_VARIANT:
			case DI_TYPE_EMPTY_OBJECT:
			case DI_TYPE_OBJECT:
			case DI_TYPE_WEAK_OBJECT:
			case DI_TYPE_STRING:
			case DI_TYPE_STRING_LITERAL:
			case DI_TYPE_POINTER:
			case DI_LAST_TYPE:
			default:
				return -EINVAL;
			}
#undef convert_case
			return 0;
		}
	}

	// float -> integer not allowed
	return -EINVAL;
}

#define di_cast_borrowed(dst, src)                                                       \
	di_type_conversion(di_typeof(src), (di_value *)&src, di_typeof(dst),             \
	                   (di_value *)&dst, true)
