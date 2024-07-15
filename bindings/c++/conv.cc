#include <deai/c++/conv.hh>
#include <limits>

namespace {

struct DeaiCheckedIntConverter {
private:
	union {
		uint64_t u;
		int64_t i;
	};
	bool is_unsigned;

public:
	DeaiCheckedIntConverter(int64_t value) : i(value), is_unsigned(false) {
	}
	DeaiCheckedIntConverter(uint64_t value) : u(value), is_unsigned(true) {
	}
	DeaiCheckedIntConverter(int value) : i(value), is_unsigned(false) {
	}
	DeaiCheckedIntConverter(unsigned int value) : u(value), is_unsigned(true) {
	}
	operator double() const {
		if (is_unsigned) {
			return static_cast<double>(u);
		}
		return static_cast<double>(i);
	}
#define DEFINE_CONVERTER(bits, pri, pru)                                                 \
	operator std::optional<int##bits##_t>() const {                                      \
		if ((is_unsigned && u > std::numeric_limits<int##bits##_t>::max()) ||            \
		    (!is_unsigned && (i > std::numeric_limits<int##bits##_t>::max() ||           \
		                      i < std::numeric_limits<int##bits##_t>::min()))) {         \
			return std::nullopt;                                                         \
		}                                                                                \
		if (is_unsigned) {                                                               \
			return static_cast<int>(u);                                                  \
		}                                                                                \
		return static_cast<int>(i);                                                      \
	}                                                                                    \
	operator std::optional<uint##bits##_t>() const {                                     \
		if (!is_unsigned && i < 0) {                                                     \
			return std::nullopt;                                                         \
		}                                                                                \
		unsigned int ret = is_unsigned ? static_cast<uint##bits##_t>(u)                  \
		                               : static_cast<uint##bits##_t>(i);                 \
		if ((is_unsigned && ret != u) || (!is_unsigned && ret != i)) {                   \
			return std::nullopt;                                                         \
		}                                                                                \
		return ret;                                                                      \
	}
	DEFINE_CONVERTER(8, PRId8, PRIu8)
	DEFINE_CONVERTER(16, PRId16, PRIu16)
	DEFINE_CONVERTER(32, PRId32, PRIu32)
	operator std::optional<int64_t>() const {
		if (!is_unsigned) {
			return i;
		}
		if (u > std::numeric_limits<int64_t>::max()) {
			return std::nullopt;
		}
		return static_cast<int64_t>(u);
	}
	operator std::optional<uint64_t>() const {
		if (is_unsigned) {
			return u;
		}
		if (i < 0) {
			return std::nullopt;
		}
		return static_cast<uint64_t>(i);
	}
};

template <bool borrow>
struct DeaiStringLiteralConverter {
	const char *value;
	operator const char *() const {
		return value;
	}
	operator ::deai::c_api::String() const {
		if constexpr (borrow) {
			return ::deai::c_api::string::borrow(value);
		}
		// string literal doesn't have a transferrable ownership,
		// so we have to create a fully owned copy if we want to move it.
		return ::deai::c_api::string::ndup(value, ::strlen(value));
	}
};

}        // namespace

namespace deai::type::conv::c_api {

template <bool borrow>
auto DeaiVariantConverter<borrow>::tuple_to_array() -> std::optional<::di_array> {
	// UNSAFE! this->type must be di_type::TUPLE
	auto &tuple = value().tuple;
	if (tuple.length == 0) {
		return ::di_array{0, nullptr, di_type::NIL};
	}
	if (tuple.length == 1) {
		return ::di_array{1, tuple.elements[0].value, tuple.elements[0].type};
	}
	if constexpr (borrow) {
		return std::nullopt;
	}
	auto elem_type = tuple.elements[0].type;
	const auto elem_size = ::deai::c_api::type::sizeof_(elem_type);
	for (size_t i = 1; i < tuple.length; i++) {
		auto elem_i_type = tuple.elements[i].type;
		if (elem_i_type != elem_type) {
			return std::nullopt;
		}
	}
	::di_array ret{tuple.length, nullptr, elem_type};
	ret.arr = ::malloc(tuple.length * elem_size);
	for (size_t i = 0; i < tuple.length; i++) {
		auto *ptr = static_cast<std::byte *>(ret.arr) + i * elem_size;
		::memcpy(ptr, tuple.elements[i].value, elem_size);
		::free(tuple.elements[i].value);
	}
	free(tuple.elements);
	tuple.length = 0;
	tuple.elements = nullptr;
	type = di_type::NIL;
	return ret;
}
template <bool borrow>
auto DeaiVariantConverter<borrow>::array_to_tuple() -> std::optional<::di_tuple> {
	// UNSAFE! this->type must be di_type::ARRAY
	auto &array = value().array;
	if (array.length == 0) {
		return ::di_tuple{0, nullptr};
	}
	if constexpr (borrow) {
		return std::nullopt;
	}
	auto elem_type = array.elem_type;
	auto elem_size = ::deai::c_api::type::sizeof_(elem_type);
	auto *elements =
	    static_cast<::di_variant *>(::malloc(array.length * sizeof(::di_variant)));
	for (size_t i = 0; i < array.length; i++) {
		auto *ptr = reinterpret_cast<std::byte *>(array.arr) + i * elem_size;
		auto *ptr_cloned = reinterpret_cast<::di_value *>(::malloc(elem_size));
		::memcpy(ptr_cloned, ptr, elem_size);
		elements[i] = ::di_variant{ptr_cloned, elem_type};
	}
	::free(array.arr);
	type = di_type::NIL;
	return ::di_tuple{array.length, elements};
}
/// Unwrap the current converter, and continue the conversion with the inner value.
/// The current converter will be emptied.
///
/// Unwrappable types are:
///
/// - A variant
/// - A tuple with a single element
/// - An array with a single element
template <bool borrow>
template <typename T>
    requires typeinfo::Verbatim<T> || DeaiNumber<T>
auto DeaiVariantConverter<borrow>::try_from_inner() -> std::optional<T> {
	using ::di_value;
	auto &v = value();
	di_value *tmp_value = nullptr;
	di_type tmp_type = di_type::NIL;
	if (type == di_type::VARIANT) {
		tmp_value = v.variant.value;
		tmp_type = v.variant.type;
	} else if (type == di_type::TUPLE && v.tuple.length == 1) {
		tmp_value = v.tuple.elements[0].value;
		tmp_type = v.tuple.elements[0].type;
	} else if (type == di_type::ARRAY && v.array.length == 1) {
		tmp_value = reinterpret_cast<::deai::c_api::Value *>(v.array.arr);
		tmp_type = v.array.elem_type;
	} else {
		return std::nullopt;
	}
	if constexpr (!borrow) {
		if (type == di_type::TUPLE) {
			::free(v.tuple.elements);
		}
		::memset(&v, 0, sizeof(v));
		type = di_type::NIL;

		di_value new_value{};
		auto type_size = ::deai::c_api::type::sizeof_(tmp_type);
		if (type_size > 0) {
			// Have to use memcpy instead of assigning, because `tmp_value` might not be "full sized".
			::memcpy(&new_value, tmp_value, type_size);
		}
		::free(tmp_value);
		return DeaiVariantConverter{std::move(new_value), std::move(tmp_type)};
	} else {
		return DeaiVariantConverter{std::ref(*v.variant.value), v.variant.type};
	}
}
template <bool borrow>
template <DeaiNumber T>
DeaiVariantConverter<borrow>::operator std::optional<T>() {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
	switch (type) {
	case di_type::INT:
		return DeaiCheckedIntConverter{value().int_};
	case di_type::UINT:
		return DeaiCheckedIntConverter{value().uint};
	case di_type::NINT:
		return DeaiCheckedIntConverter{value().nint};
	case di_type::NUINT:
		return DeaiCheckedIntConverter{value().nuint};
	case di_type::FLOAT:
		if constexpr (std::is_same_v<T, double>) {
			return value().float_;
		}
		return std::nullopt;
	default:
		break;
	}
	return try_from_inner<T>();
#pragma GCC diagnostic pop
}

template <bool borrow>
DeaiVariantConverter<borrow>::operator std::optional<::di_variant>() {
	if constexpr (borrow) {
		return ::di_variant{&value(), type};
	}

	::di_variant ret{nullptr, type};
	ret.value = reinterpret_cast<::di_value *>(::malloc(::deai::c_api::type::sizeof_(type)));
	::memcpy(ret.value, &value(), ::deai::c_api::type::sizeof_(type));
	return ret;
}
template <bool borrow>
DeaiVariantConverter<borrow>::operator std::optional<::deai::c_api::String>() {
	if (type == di_type::STRING) {
		type = di_type::NIL;
		return value().string;
	}
	if (type == di_type::STRING_LITERAL) {
		return DeaiStringLiteralConverter<borrow>(value().string_literal);
	}
	return try_from_inner<::deai::c_api::String>();
}
template <bool borrow>
DeaiVariantConverter<borrow>::operator std::optional<const char *>() {
	if (type == di_type::STRING_LITERAL) {
		return value().string_literal;
	}
	return try_from_inner<const char *>();
}
template <bool borrow>
DeaiVariantConverter<borrow>::operator std::optional<::di_array>() {
	if (type == di_type::ARRAY) {
		type = di_type::NIL;
		return value().array;
	}
	if (type == di_type::NIL) {
		return ::di_array{0, nullptr, di_type::NIL};
	}
	if (type == di_type::EMPTY_OBJECT) {
		return ::di_array{0, nullptr, di_type::NIL};
	}
	if (type == di_type::TUPLE) {
		return tuple_to_array();
	}
	return try_from_inner<::di_array>();
}
template <bool borrow>
DeaiVariantConverter<borrow>::operator std::optional<::di_tuple>() {
	if (type == di_type::TUPLE) {
		type = di_type::NIL;
		return value().tuple;
	}
	if (type == di_type::NIL) {
		return ::di_tuple{0, nullptr};
	}
	if (type == di_type::EMPTY_OBJECT) {
		return ::di_tuple{0, nullptr};
	}
	if (type == di_type::ARRAY) {
		return array_to_tuple();
	}
	return try_from_inner<::di_tuple>();
}
template <bool borrow>
DeaiVariantConverter<borrow>::operator std::optional<::di_object *>() {
	if (type == di_type::OBJECT || type == di_type::EMPTY_OBJECT) {
		type = di_type::NIL;
		return value().object;
	}
	if (type == di_type::WEAK_OBJECT) {
		if constexpr (!borrow) {
			auto ret = ::di_upgrade_weak_ref(value().weak_object);
			if (ret == nullptr) {
				return std::nullopt;
			}
			return ret;
		}
	}
	return try_from_inner<::di_object *>();
}
template <bool borrow>
DeaiVariantConverter<borrow>::operator std::optional<::di_weak_object *>() {
	if (type == di_type::WEAK_OBJECT) {
		type = di_type::NIL;
		return value().weak_object;
	}
	if (type == di_type::OBJECT || type == di_type::EMPTY_OBJECT) {
		if constexpr (borrow) {
			return reinterpret_cast<::di_weak_object *>(value().object);
		}
		return ::di_weakly_ref_object(value().object);
	}
	if (type == di_type::NIL) {
		return ::dead_weak_ref;
	}
	return try_from_inner<::di_weak_object *>();
}
template <bool borrow>
DeaiVariantConverter<borrow>::operator std::optional<void *>() {
	if (type == di_type::NIL) {
		return nullptr;
	}
	if (type == di_type::POINTER) {
		return value().pointer;
	}
	return try_from_inner<void *>();
}
template <bool borrow>
DeaiVariantConverter<borrow>::operator std::optional<bool>() {
	if (type == di_type::BOOL) {
		return value().bool_;
	}
	return try_from_inner<bool>();
}
template struct DeaiVariantConverter<true>;
template struct DeaiVariantConverter<false>;
template DeaiVariantConverter<true>::operator std::optional<int>();
template DeaiVariantConverter<true>::operator std::optional<unsigned int>();
template DeaiVariantConverter<true>::operator std::optional<int64_t>();
template DeaiVariantConverter<true>::operator std::optional<uint64_t>();
template DeaiVariantConverter<true>::operator std::optional<double>();
template DeaiVariantConverter<false>::operator std::optional<int>();
template DeaiVariantConverter<false>::operator std::optional<unsigned int>();
template DeaiVariantConverter<false>::operator std::optional<int64_t>();
template DeaiVariantConverter<false>::operator std::optional<uint64_t>();
template DeaiVariantConverter<false>::operator std::optional<double>();
}        // namespace deai::type::conv::c_api

namespace {
using ::deai::type::conv::c_api::DeaiVariantConverter;
template <bool borrow>
auto di_type_conversion_impl(DeaiVariantConverter<borrow> &&converter, ::di_type to_type,
                             ::di_value &to) -> int {
#define CONVERT_TO(T, field)                                                             \
	case di_type::T: {                                                                   \
		std::optional<decltype(to.field)> tmp = converter;                               \
		if (!tmp.has_value()) {                                                          \
			return -EINVAL;                                                              \
		}                                                                                \
		to.field = *tmp;                                                                 \
		break;                                                                           \
	}

	// clang-format off
	switch (to_type) {
	CONVERT_TO(INT, int_);
	CONVERT_TO(UINT, uint);
	CONVERT_TO(NINT, nint);
	CONVERT_TO(NUINT, nuint);
	CONVERT_TO(FLOAT, float_);
	CONVERT_TO(BOOL, bool_);
	CONVERT_TO(STRING, string);
	CONVERT_TO(STRING_LITERAL, string_literal);
	CONVERT_TO(ARRAY, array);
	CONVERT_TO(TUPLE, tuple);
	CONVERT_TO(OBJECT, object);
	CONVERT_TO(WEAK_OBJECT, weak_object);
	CONVERT_TO(POINTER, pointer);
	CONVERT_TO(VARIANT, variant);
	case di_type::NIL:
	case di_type::EMPTY_OBJECT:
	case di_type::ANY:
	case di_type::DI_LAST_TYPE:
		return -EINVAL;
	}
// clang-format on
#undef CONVERT_TO
	return 0;
}        // namespace
}        // namespace

extern "C" {
auto di_type_conversion(di_type from_type, di_value *from, di_type to_type, di_value *to,
                        bool borrowing) -> int {
	if (from_type == to_type) {
		::memcpy(to, from, ::deai::c_api::type::sizeof_(from_type));
		return 0;
	}
	if (borrowing) {
		return di_type_conversion_impl<true>(
		    DeaiVariantConverter<true>{std::ref(*from), from_type}, to_type, *to);
	}
	return di_type_conversion_impl<false>(
	    DeaiVariantConverter<false>{std::move(*from), std::move(from_type)}, to_type, *to);
}
auto di_int_conversion(di_type from_type, di_value *from, int to_bits, bool to_unsigned,
                       void *to) -> int {
	deai::type::conv::c_api::DeaiVariantConverter<true> converter{std::ref(*from), from_type};

#define EXPAND                                                                           \
	X(8, P);                                                                             \
	X(16, P);                                                                            \
	X(32, P);                                                                            \
	X(64, P)

#define X_(a, b)                                                                         \
	case a: {                                                                            \
		std::optional<b##a##_t> tmp = converter;                                         \
		if (!tmp.has_value()) {                                                          \
			return -EINVAL;                                                              \
		}                                                                                \
		*reinterpret_cast<b##a##_t *>(to) = *tmp;                                        \
		break;                                                                           \
	}
#define X(a, b) X_(a, b)

	if (!to_unsigned) {
#define P int
		switch (to_bits) {
			EXPAND;
		default:
			abort();
		}
#undef P
		return 0;
	}

#define P uint
	switch (to_bits) {
		EXPAND;
	default:
		abort();
	}
#undef EXPAND
#undef P
#undef X
	return 0;
}
}
