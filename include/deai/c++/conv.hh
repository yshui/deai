#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <typeinfo>

#include "c_api.hh"        // IWYU pragma: keep
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
	return ::deai::c_api::String{str.c_str(), str.size()};
}

inline auto string_to_borrowed_deai_value(const ::deai::c_api::String &str) {
	return str;
}

inline auto string_to_borrowed_deai_value(const std::string_view &str) {
	return ::deai::c_api::String{str.data(), str.size()};
}

template <typename T, size_t length>
    requires(typeinfo::is_basic_deai_type(typeinfo::of<T>::value))
auto array_to_borrowed_deai_value(const std::array<T, length> &arr) -> ::deai::c_api::Array {
	return ::deai::c_api::Array{length, arr.data(), typeinfo::of<T>::value};
}

template <typeinfo::Verbatim T>
auto array_to_borrowed_deai_value(const std::vector<T> &arr) -> ::deai::c_api::Array {
	return ::deai::c_api::Array{arr.size(), arr.data(), typeinfo::of<T>::value};
}

inline auto array_to_borrowed_deai_value(const ::deai::c_api::Array &arr) {
	return arr;
}

/// Borrow a C++ value into a deai value
template <typename T>
auto to_borrowed_deai_value(const T &input) {
	static constexpr auto type = typeinfo::of<std::remove_cvref_t<T>>::value;
	if constexpr (typeinfo::is_verbatim_v<std::remove_cvref_t<T>>) {
		return input;
	} else if constexpr (type == c_api::Type::OBJECT) {
		return &input->base;
	} else if constexpr (type == c_api::Type::ARRAY) {
		return array_to_borrowed_deai_value(input);
	} else if constexpr (type == c_api::Type::STRING) {
		return string_to_borrowed_deai_value(input);
	}
	unreachable();
}

/// Borrow a C++ value into a deai variant
template <typename T>
auto to_borrowed_deai_variant(const T &input) -> ::di_variant {
	static constexpr auto type = typeinfo::of<std::remove_cvref_t<T>>::value;
	auto *value_ptr = reinterpret_cast<::di_value *>(::malloc(sizeof(::di_value)));
	auto value = to_borrowed_deai_value(input);
	::memcpy(value_ptr, &value, sizeof(value));
	return {.value = value, .type = type};
}

inline auto string_to_owned_deai_value(std::string &&input) -> ::deai::c_api::String {
	auto moved = std::move(input);
	return ::deai::c_api::string::ndup(moved.c_str(), moved.size());
}

template <typeinfo::Convertible T>
inline auto array_to_owned_deai_value(std::vector<T> arr) -> ::deai::c_api::Array;

/// Convert an owned C++ value to an owned deai value. Mostly the same as the borrowed
/// case, except for strings and arrays
template <typename T>
    requires typeinfo::Convertible<std::remove_cvref_t<T>>
auto to_owned_deai_value(T &&input) {
	static constexpr auto type = typeinfo::of<std::remove_cvref_t<T>>::value;
	if constexpr (typeinfo::is_verbatim_v<std::remove_cvref_t<T>>) {
		return input;
	} else if constexpr (type == c_api::Type::VARIANT) {
		return static_cast<::di_variant>(std::forward<T>(input));
	} else if constexpr (type == c_api::Type::OBJECT || type == c_api::Type::WEAK_OBJECT) {
		auto tmp_object = std::forward<T>(input);        // Copy or move the object
		return std::move(tmp_object).release();
	} else if constexpr (type == c_api::Type::STRING) {
		return string_to_owned_deai_value(std::forward<T>(input));
	} else if constexpr (type == c_api::Type::ARRAY) {
		return array_to_owned_deai_value(input);
	}
	unreachable();
}
template <typeinfo::Convertible T>
inline auto array_to_owned_deai_value(std::vector<T> arr) -> c_api::Array {
	static constexpr auto type = typeinfo::of<T>::value;
	if (arr.empty()) {
		return ::deai::c_api::Array{0, nullptr, ::di_type::NIL};
	}
	auto elem_size = c_api::type::sizeof_(type);
	auto data = ::malloc(arr.size() * elem_size);
	if (data == nullptr) {
		throw std::bad_alloc();
	}
	for (size_t i = 0; i < arr.size(); i++) {
		auto *ptr = static_cast<std::byte *>(data) + i * elem_size;
		auto value = to_owned_deai_value(std::move(arr[i]));
		::memcpy(ptr, &value, elem_size);
	}
	return ::deai::c_api::Array{arr.size(), data, type};
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
    typename typeinfo::deai_ctype_t<typeinfo::of<std::remove_cvref_t<T>>::value>;

template <typename T>
using to_owned_deai_type = typename to_owned_deai_type_helper<T>::type;

template <typename T>
auto to_borrowed_deai_value_union(const T &input) -> ::di_value {
	::di_value ret;
	auto tmp = to_borrowed_deai_value(input);
	std::memcpy(&ret, &tmp, sizeof(tmp));
	return ret;
}

template <typename... Args>
auto to_borrowed_deai_values(const Args &...args) -> std::array<::di_value, sizeof...(Args)> {
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
    requires typeinfo::Verbatim<std::remove_reference_t<T>>
auto to_borrowed_cpp_value(T &&arg) -> T {
	return std::forward<T>(arg);
}

inline auto to_borrowed_cpp_value(::deai::c_api::Array arg);
inline auto to_borrowed_cpp_value(::deai::c_api::String arg);

template <typeinfo::Convertible T>
auto variant_to_borrowed_cpp_value(void *ptr_) {
	auto *ptr = reinterpret_cast<to_deai_ctype<T> *>(ptr_);
	return to_borrowed_cpp_value(*ptr);
}

struct DeaiBorrowedArrayConverter {
	::deai::c_api::Array arg;
	// If the target is a std::span of raw deai types, then we can convert it
	// directly. Otherwise we have to create a std::vector and copy each element.
	template <typeinfo::Verbatim T>
	operator std::span<T>() {
		return {static_cast<T *>(arg.arr), arg.length};
	}
	template <typeinfo::Convertible T>
	operator std::vector<T>() const;
	operator ::deai::c_api::Array() const {
		return arg;
	}
};

inline auto to_borrowed_cpp_value(::deai::c_api::Array arg) {
	return DeaiBorrowedArrayConverter{arg};
}

inline auto to_borrowed_cpp_value(::deai::c_api::String arg) {
	struct DeaiStringConvert {
		::deai::c_api::String arg;
		operator std::string_view() const {
			return {arg.data, arg.length};
		}
		operator std::string() const {
			return {arg.data, arg.length};
		}
		operator ::deai::c_api::String() const {
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

/// Convert a deai variant into another deai type. This converter either moves or borrows,
/// depending on the template parameter. If doing moving conversion, the input value will
/// be moved into the converter first, and then moved into the output value of the desired
/// type. Otherwise, the input value will be borrowed.
template <bool borrow>
struct DeaiVariantConverter {
private:
	template <typename T>
	    requires borrow
	static auto ref_if_borrow() -> std::reference_wrapper<T>;
	template <typename T>
	    requires(!borrow)
	static auto ref_if_borrow() -> T;
	using value_type = decltype(ref_if_borrow<::di_value>());

	template <typename T>
	    requires borrow
	static auto move_if_not_borrow() -> const T;
	template <typename T>
	    requires(!borrow)
	static auto move_if_not_borrow() -> T &&;
	template <typename T>
	using move_if_not_borrow_t = decltype(move_if_not_borrow<T>());
	using ctor_value_type = move_if_not_borrow_t<value_type>;

	using di_type = ::di_type;
	auto tuple_to_array() -> std::optional<::deai::c_api::Array>;
	auto array_to_tuple() -> std::optional<::di_tuple>;

	/// Unwrap the current converter, and continue the conversion with the inner value.
	/// The current converter will be emptied.
	///
	/// Unwrappable types are:
	///
	/// - A variant
	/// - A tuple with a single element
	/// - An array with a single element
	template <typename T>
	    requires typeinfo::Verbatim<T> || DeaiNumber<T>
	auto try_from_inner() -> std::optional<T>;

	value_type value_;
	di_type type;

	auto value() -> ::di_value & {
		if constexpr (borrow) {
			return value_.get();
		} else {
			return value_;
		}
	}

public:
	DeaiVariantConverter(ctor_value_type in_value, move_if_not_borrow_t<di_type> in_type);
	~DeaiVariantConverter() {
		if constexpr (!borrow) {
			::di_free_value(type, &value_);
		}
	}
	template <DeaiNumber T>
	operator std::optional<T>();
	operator std::optional<::deai::c_api::Variant>();
	operator std::optional<::deai::c_api::String>();
	operator std::optional<const char *>();
	operator std::optional<::deai::c_api::Array>();
	operator std::optional<::deai::c_api::Tuple>();
	operator std::optional<::deai::c_api::Object *>();
	operator std::optional<::deai::c_api::WeakObject *>();
	operator std::optional<void *>();
	operator std::optional<bool>();
};

template <>
DeaiVariantConverter<true>::DeaiVariantConverter(std::reference_wrapper<::deai::c_api::Value> in_value,
                                                 ::deai::c_api::Type in_type);

template <>
DeaiVariantConverter<false>::DeaiVariantConverter(::deai::c_api::Value &&in_value,
                                                  ::deai::c_api::Type &&in_type);

extern template struct DeaiVariantConverter<true>;
extern template struct DeaiVariantConverter<false>;

/// Convert a borrowed deai value one type to another. There is no copying or
/// ownership changes involved.
template <typeinfo::Verbatim S>
auto borrow_from_variant(::di_value &value, ::di_type type) -> S {
	static constexpr auto target_type = typeinfo::of<S>::value;
	if (type == target_type) {
		return *reinterpret_cast<S *>(&value);
	}
	DeaiVariantConverter<true> impl{value, type};
	return *static_cast<std::optional<S>>(impl);
}

}        // namespace c_api

template <typeinfo::Convertible T>
DeaiBorrowedArrayConverter::operator std::vector<T>() const {
	static constexpr auto Type = typeinfo::of<T>::value;
	std::vector<T> ret;
	ret.reserve(arg.length);
	if (arg.length == 0) {
		return ret;
	}

	auto elem_size = ::di_sizeof_type(arg.elem_type);
	for (size_t i = 0; i < arg.length; i++) {
		auto *ptr = reinterpret_cast<std::byte *>(arg.arr) + i * elem_size;
		if (arg.elem_type != Type) {
			std::optional<to_deai_ctype<T>> converted = c_api::DeaiVariantConverter<true>{
			    *reinterpret_cast<::di_value *>(ptr), arg.elem_type};
			if (!converted.has_value()) {
				throw ::di_new_error(
				    "Array element type mismatch, %s cannot be converted into %s",
				    ::di_type_names[static_cast<int>(arg.elem_type)], typeid(T).name());
			}
			ret.push_back(variant_to_borrowed_cpp_value<T>(&converted.value()));
		} else {
			ret.push_back(variant_to_borrowed_cpp_value<T>(ptr));
		}
	}
	return ret;
}

}        // namespace deai::type::conv
