#pragma once

#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <typeinfo>

#include "c_api.hh"
#include "typeinfo.hh"

namespace deai::type::conv {

/// Concatenate two std::arrays
template <typename Element, size_t length1, size_t length2>
constexpr auto array_cat(std::array<Element, length1> a, std::array<Element, length2> b)
    -> std::array<Element, length1 + length2> {
	std::array<Element, length1 + length2> output;
	std::copy(a.begin(), a.end(), output.begin());
	std::copy(b.begin(), b.end(), output.begin() + length1);
	return output;
}

inline auto string_to_borrowed_deai_value(const std::string &str) {
	return c_api::di_string{str.c_str(), str.size()};
}

inline auto string_to_borrowed_deai_value(const c_api::di_string &str) {
	return str;
}

inline auto string_to_borrowed_deai_value(const std::string_view &str) {
	return c_api::di_string{str.data(), str.size()};
}

template <typename T, size_t length, c_api::di_type type = id::deai_typeof<T>::value>
auto array_to_borrowed_deai_value(const std::array<T, length> &arr)
    -> std::enable_if<id::is_basic_deai_type(type), c_api::di_array> {
	return c_api::di_array{length, arr.data(), type};
}

template <id::DeaiVerbatim T, c_api::di_type type = id::deai_typeof<T>::value>
auto array_to_borrowed_deai_value(const std::vector<T> &arr) -> c_api::di_array {
	return c_api::di_array{arr.size(), arr.data(), type};
}

inline auto array_to_borrowed_deai_value(const c_api::di_array &arr) {
	return arr;
}

/// Borrow a C++ value into a deai value
template <typename T, c_api::di_type type = id::deai_typeof<typename std::remove_reference<T>::type>::value>
auto to_borrowed_deai_value(const T &input) {
	if constexpr (id::is_verbatim_v<typename std::remove_reference<T>::type>) {
		return input;
	} else if constexpr (type == c_api::di_type::OBJECT) {
		return input.raw();
	} else if constexpr (type == c_api::di_type::ARRAY) {
		return array_to_borrowed_deai_value(input);
	} else if constexpr (type == c_api::di_type::STRING) {
		return string_to_borrowed_deai_value(input);
	}
	unreachable();
}

/// Borrow a C++ value into a deai variant
template <typename T, c_api::di_type type = id::deai_typeof<typename std::remove_reference<T>::type>::value>
auto to_borrowed_deai_variant(const T &input) -> c_api::di_variant {
	auto *value_ptr = reinterpret_cast<c_api::di_value *>(::malloc(sizeof(c_api::di_value)));
	auto value = to_borrowed_deai_value(input);
	::memcpy(value_ptr, &value, sizeof(value));
	return {.value = value, .type = type};
}

inline auto string_to_owned_deai_value(std::string &&input) -> c_api::di_string {
	auto moved = std::move(input);
	return c_api::di_string_ndup(moved.c_str(), moved.size());
}

template <id::DeaiConvertible T, c_api::di_type Type = id::deai_typeof<T>::value>
inline auto array_to_owned_deai_value(std::vector<T> arr) -> c_api::di_array;

/// Convert an owned C++ value to an owned deai value. Mostly the same as the borrowed
/// case, except for strings and arrays
template <typename T, c_api::di_type type = id::deai_typeof<std::remove_cvref_t<T>>::value>
auto to_owned_deai_value(T &&input) {
	if constexpr (id::is_verbatim_v<std::remove_cvref_t<T>>) {
		return input;
	} else if constexpr (type == c_api::di_type::VARIANT) {
		return static_cast<c_api::di_variant>(std::forward<T>(input));
	} else if constexpr (type == c_api::di_type::OBJECT || type == c_api::di_type::WEAK_OBJECT) {
		auto tmp_object = std::forward<T>(input);        // Copy or move the object
		return std::move(tmp_object).release();
	} else if constexpr (type == c_api::di_type::STRING) {
		return string_to_owned_deai_value(std::forward<T>(input));
	} else if constexpr (type == c_api::di_type::ARRAY) {
		return array_to_owned_deai_value(input);
	}
	unreachable();
}
template <id::DeaiConvertible T, c_api::di_type Type>
inline auto array_to_owned_deai_value(std::vector<T> arr) -> c_api::di_array {
	if (arr.empty()) {
		return c_api::di_array{0, nullptr, c_api::di_type::NIL};
	}
	auto elem_size = c_api::di_sizeof_type(Type);
	auto data = ::malloc(arr.size() * elem_size);
	if (data == nullptr) {
		throw std::bad_alloc();
	}
	for (size_t i = 0; i < arr.size(); i++) {
		auto *ptr = static_cast<std::byte *>(data) + i * elem_size;
		auto value = to_owned_deai_value(std::move(arr[i]));
		::memcpy(ptr, &value, elem_size);
	}
	return c_api::di_array{arr.size(), data, Type};
}

template <typename T>
using to_borrowed_deai_type = decltype(to_borrowed_deai_value(std::declval<T>()));

template <typename T>
struct to_owned_deai_type_helper {
	using type = decltype(to_owned_deai_value(std::declval<T>()));
};

template <>
struct to_owned_deai_type_helper<void> {
	using type = void;
};

template <typename T>
using to_deai_ctype =
    typename id::deai_ctype_t<id::deai_typeof<std::remove_cvref_t<T>>::value>;

template <typename T>
using to_owned_deai_type = typename to_owned_deai_type_helper<T>::type;

template <typename T>
auto to_borrowed_deai_value_union(const T &input) -> c_api::di_value {
	c_api::di_value ret;
	auto tmp = to_borrowed_deai_value(input);
	std::memcpy(&ret, &tmp, sizeof(tmp));
	return ret;
}

template <typename... Args>
auto to_borrowed_deai_values(const Args &...args)
    -> std::array<c_api::di_value, sizeof...(Args)> {
	return std::array{to_borrowed_deai_value_union(args)...};
}

/// Convert a borrowed di_value to a borrowed C++ value. This is not a complete inverse of
/// to_borrowed_deai_value. Because creating some C++ value in its borrowed form is
/// impossible. e.g. you cannot create a borrowed std::string, or e.g. Ref<Object> always
/// owns its object.
///
/// Also, the deai value might be cloned nonetheless. For example, a di_array has to be
/// cloned into a vector.
template <typename T>
    requires id::DeaiVerbatim<std::remove_reference_t<T>>
auto to_borrowed_cpp_value(T &&arg) -> T {
	return std::forward<T>(arg);
}

inline auto to_borrowed_cpp_value(c_api::di_array arg);
inline auto to_borrowed_cpp_value(c_api::di_string arg);

struct DeaiArrayConvert {
	c_api::di_array arg;
	// If the target is a std::span of raw deai types, then we can convert it
	// directly. Otherwise we have to create a std::vector and copy each element.
	template <id::DeaiVerbatim T, c_api::di_type Type = id::deai_typeof<T>::value>
	operator std::span<T>() {
		return {static_cast<T *>(arg.arr), arg.length};
	}
	template <id::DeaiConvertible T, c_api::di_type Type = id::deai_typeof<T>::value>
	operator std::vector<T>() const {
		if (arg.elem_type != Type) {
			throw c_api::di_new_error(
			    "Array element type mismatch, %s cannot be converted into %s",
			    c_api::di_type_names[static_cast<int>(arg.elem_type)],
			    typeid(T).name());
		}
		std::vector<T> ret;
		ret.reserve(arg.length);

		auto elem_size = c_api::di_sizeof_type(Type);
		for (size_t i = 0; i < arg.length; i++) {
			auto *ptr = reinterpret_cast<id::deai_ctype_t<Type> *>(
			    reinterpret_cast<std::byte *>(arg.arr) + i * elem_size);
			ret.push_back(to_borrowed_cpp_value(*ptr));
		}
		return ret;
	}
	operator c_api::di_array() const {
		return arg;
	}
};

inline auto to_borrowed_cpp_value(c_api::di_array arg) {
	return DeaiArrayConvert{arg};
}

inline auto to_borrowed_cpp_value(c_api::di_string arg) {
	struct DeaiStringConvert {
		c_api::di_string arg;
		operator std::string_view() const {
			return {arg.data, arg.length};
		}
		operator std::string() const {
			return {arg.data, arg.length};
		}
		operator c_api::di_string() const {
			return arg;
		}
	};
	return DeaiStringConvert{arg};
}

/// Conversion between C API deai values
namespace c_api {

template <typename T>
concept DeaiNumber =
    std::is_same_v<T, int8_t> || std::is_same_v<T, uint8_t> || std::is_same_v<T, int16_t> ||
    std::is_same_v<T, uint16_t> || std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t> ||
    std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t> || std::is_same_v<T, double>;
template <bool borrow>
struct DeaiVariantConverter {
	private:
	template <typename T>
	requires borrow
	static auto ref_if_borrow() -> std::reference_wrapper<T>;
	template <typename T>
	requires(!borrow)
	static auto ref_if_borrow() -> T;

	using di_type = ::deai::c_api::di_type;
	auto tuple_to_array() -> std::optional<::deai::c_api::di_array>;
	auto array_to_tuple() -> std::optional<::deai::c_api::di_tuple>;
	/// Unwrap the current converter, and return a converter for the inner variant.
	/// The current converter will be emptied.
	auto unwrap_variant() -> DeaiVariantConverter;
	decltype(ref_if_borrow<::deai::c_api::di_value>()) value_;
	di_type type;

	auto value() -> ::deai::c_api::di_value & {
		if constexpr (borrow) {
			return value_.get();
		} else {
			return value_;
		}
	}

	public:
	DeaiVariantConverter(decltype(value_) value, di_type type)
	    : value_(value), type(type) {
	}
	template <DeaiNumber T>
	operator std::optional<T>();
	operator std::optional<::deai::c_api::di_variant>();
	operator std::optional<::deai::c_api::di_string>();
	operator std::optional<const char *>();
	operator std::optional<::deai::c_api::di_array>();
	operator std::optional<::deai::c_api::di_tuple>();
	operator std::optional<::deai::c_api::di_object *>();
	operator std::optional<::deai::c_api::di_weak_object *>();
	operator std::optional<void *>();
	operator std::optional<bool>();
};

extern template struct DeaiVariantConverter<true>;
extern template struct DeaiVariantConverter<false>;

}        // namespace c_api

}        // namespace deai::type::conv
