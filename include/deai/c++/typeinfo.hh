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
struct Variant;
}

namespace typeinfo {

/// A type that is "derived" from the c_api::Object
template <typename T>
concept DerivedObject = requires(T t) {
	// requires std::is_standard_layout_v<T>;
	{ &t.base } -> std::same_as<c_api::Object *>;
	requires offsetof(T, base) == 0;
	{ T::type } -> std::same_as<const char (&)[sizeof(T::type)]>;
};
}        // namespace typeinfo

namespace type {
template <typeinfo::DerivedObject T>
struct Ref;
template <typeinfo::DerivedObject T>
struct WeakRef;
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
template <DerivedObject T>
struct of<type::Ref<T>> {
	static constexpr auto value = c_api::Type::OBJECT;
};
template <>
struct of<c_api::Object *> {
	static constexpr auto value = c_api::Type::OBJECT;
};
template <typename T>
struct of<type::WeakRef<T>> {
	static constexpr auto value = c_api::Type::WEAK_OBJECT;
};
template <>
struct of<c_api::WeakObject *> {
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
struct of<c_api::String> {
	static constexpr auto value = c_api::Type::STRING;
};
template <>
struct of<c_api::Array> {
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

template <>
struct is_verbatim<c_api::String> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<c_api::Array> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<c_api::Tuple> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<c_api::Variant> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<c_api::Object *> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<c_api::WeakObject *> {
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
	using type = c_api::String;
};

template <>
struct deai_ctype<c_api::Type::POINTER> {
	using type = void *;
};

template <>
struct deai_ctype<c_api::Type::OBJECT> {
	using type = c_api::Object *;
};

template <>
struct deai_ctype<c_api::Type::WEAK_OBJECT> {
	using type = c_api::WeakObject *;
};

template <>
struct deai_ctype<c_api::Type::ARRAY> {
	using type = c_api::Array;
};

template <>
struct deai_ctype<c_api::Type::TUPLE> {
	using type = c_api::Tuple;
};

template <>
struct deai_ctype<c_api::Type::VARIANT> {
	using type = c_api::Variant;
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
