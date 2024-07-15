#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "c_api.hh"        // IWYU pragma: keep

namespace deai {
namespace type {
struct Object;
template <typename T>
    requires std::derived_from<T, type::Object>
struct Ref;
template <typename T>
struct WeakRef;
struct Variant;
}        // namespace type
namespace typeinfo {
template <c_api::Type type>
struct deai_ctype {};

template <typename T>
struct of {};

template <typename T>
concept Convertible = requires { of<T>::value; };

constexpr auto is_basic_deai_type(c_api::Type type) -> bool {
	return (type != c_api::Type::ARRAY) && (type != c_api::Type::TUPLE) &&
	       (type != c_api::Type::VARIANT);
}

/// Whether T is a c_api di_* type
template <typename T>
struct is_verbatim {
	static constexpr bool value = false;
};

template <Convertible T>
struct is_verbatim<T> {
private:
	static constexpr auto type = of<T>::value;

public:
	static constexpr bool value = is_basic_deai_type(type) && type != c_api::Type::OBJECT &&
	                              type != c_api::Type::WEAK_OBJECT &&
	                              type != c_api::Type::STRING;
};

template <typename T>
inline constexpr bool is_verbatim_v = is_verbatim<T>::value;

template <typename T>
concept Verbatim = is_verbatim_v<T>;

template <typename T>
concept ConvertibleOnly = Convertible<T> && !Verbatim<T>;

template <>
struct of<void> {
	static constexpr auto value = c_api::Type::NIL;
};
template <>
struct of<std::monostate> {
	static constexpr auto value = c_api::Type::NIL;
};
template <>
struct of<int> {
	static constexpr auto value = c_api::Type::NINT;
};
template <>
struct of<unsigned int> {
	static constexpr auto value = c_api::Type::NUINT;
};
template <>
struct of<int64_t> {
	static constexpr auto value = c_api::Type::INT;
};
template <>
struct of<uint64_t> {
	static constexpr auto value = c_api::Type::UINT;
};
template <>
struct of<double> {
	static constexpr auto value = c_api::Type::FLOAT;
};
template <>
struct of<bool> {
	static constexpr auto value = c_api::Type::BOOL;
};
template <>
struct of<void *> {
	static constexpr auto value = c_api::Type::POINTER;
};
template <typename T>
    requires std::derived_from<T, type::Object>
struct of<type::Ref<T>> {
	static constexpr auto value = c_api::Type::OBJECT;
};
template <>
struct of<::di_object *> {
	static constexpr auto value = c_api::Type::OBJECT;
};
template <typename T>
struct of<type::WeakRef<T>> {
	static constexpr auto value = c_api::Type::WEAK_OBJECT;
};
template <>
struct of<::di_weak_object *> {
	static constexpr auto value = c_api::Type::WEAK_OBJECT;
};
template <>
struct of<std::string> {
	static constexpr auto value = c_api::Type::STRING;
};
template <>
struct of<const char *> {
	static constexpr auto value = c_api::Type::STRING_LITERAL;
};
template <>
struct of<std::string_view> {
	static constexpr auto value = c_api::Type::STRING;
};
template <>
struct of<::di_string> {
	static constexpr auto value = c_api::Type::STRING;
};
template <>
struct of<::di_array> {
	static constexpr auto value = c_api::Type::ARRAY;
};
template <typename T, size_t length>
    requires(is_basic_deai_type(of<T>::value))
struct of<std::array<T, length>> {
	static constexpr auto value = c_api::Type::ARRAY;
};
template <>
struct of<::di_tuple> {
	static constexpr auto value = c_api::Type::TUPLE;
};
template <>
struct of<c_api::Variant> {
	static constexpr auto value = c_api::Type::VARIANT;
};
template <>
struct of<type::Variant> {
	static constexpr auto value = c_api::Type::VARIANT;
};

template <Verbatim T>
struct of<std::span<T>> {
	static constexpr auto value = c_api::Type::ARRAY;
};

template <Convertible T>
struct of<std::vector<T>> {
	static constexpr auto value = c_api::Type::ARRAY;
};

static_assert(of<type::Ref<type::Object>>::value == c_api::Type::OBJECT);

template <>
struct is_verbatim<::di_string> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<::di_array> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<::di_tuple> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<::di_variant> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<::di_object *> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<::di_weak_object *> {
	static constexpr bool value = true;
};

template <>
struct deai_ctype<c_api::Type::INT> {
	using type = int64_t;
};

template <>
struct deai_ctype<c_api::Type::UINT> {
	using type = uint64_t;
};

template <>
struct deai_ctype<c_api::Type::NINT> {
	using type = int;
};

template <>
struct deai_ctype<c_api::Type::NUINT> {
	using type = unsigned int;
};

template <>
struct deai_ctype<c_api::Type::FLOAT> {
	using type = double;
};

template <>
struct deai_ctype<c_api::Type::BOOL> {
	using type = bool;
};

template <>
struct deai_ctype<c_api::Type::STRING_LITERAL> {
	using type = const char *;
};

template <>
struct deai_ctype<c_api::Type::STRING> {
	using type = ::di_string;
};

template <>
struct deai_ctype<c_api::Type::POINTER> {
	using type = void *;
};

template <>
struct deai_ctype<c_api::Type::OBJECT> {
	using type = ::di_object *;
};

template <>
struct deai_ctype<c_api::Type::WEAK_OBJECT> {
	using type = ::di_weak_object *;
};

template <>
struct deai_ctype<c_api::Type::ARRAY> {
	using type = ::di_array;
};

template <>
struct deai_ctype<c_api::Type::TUPLE> {
	using type = ::di_tuple;
};

template <>
struct deai_ctype<c_api::Type::VARIANT> {
	using type = ::di_variant;
};

template <c_api::Type type>
using deai_ctype_t = typename deai_ctype<type>::type;

template <typename... Args>
constexpr auto get_deai_types() {
	return std::array<c_api::Type, sizeof...(Args)>{
	    typeinfo::of<std::remove_cvref_t<Args>>::value...};
}
template <typename T>
static constexpr bool is_trivially_convertible = [](c_api::Type type) {
	return typeinfo::is_basic_deai_type(type) && (type != c_api::Type::OBJECT) &&
	       (type != c_api::Type::NIL) && (type != c_api::Type::ANY) &&
	       (type != c_api::Type::DI_LAST_TYPE) && (type != c_api::Type::WEAK_OBJECT);
}(typeinfo::of<T>::value);
template <typename T>
concept TriviallyConvertible = is_trivially_convertible<T>;
}        // namespace typeinfo
}        // namespace deai
