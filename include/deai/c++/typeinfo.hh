#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "c_api.hh"        // IWYU pragma: keep

namespace deai::type {
struct Object;
template <typename T>
    requires std::derived_from<T, type::Object>
struct Ref;
template <typename T>
struct WeakRef;
struct Variant;

namespace id {
template <c_api::di_type type>
struct deai_ctype {};

template <typename T>
struct deai_typeof {};

template <typename T>
concept DeaiConvertible = requires { deai_typeof<T>::value; };

constexpr auto is_basic_deai_type(c_api::di_type type) -> bool {
	return (type != c_api::di_type::ARRAY) && (type != c_api::di_type::TUPLE) &&
	       (type != c_api::di_type::VARIANT);
}

/// Whether T is a c_api di_* type
template <typename T>
struct is_verbatim {
	static constexpr bool value = false;
};

template <DeaiConvertible T>
struct is_verbatim<T> {
private:
	static constexpr auto type = deai_typeof<T>::value;

public:
	static constexpr bool value =
	    is_basic_deai_type(type) && type != c_api::di_type::OBJECT &&
	    type != c_api::di_type::WEAK_OBJECT && type != c_api::di_type::STRING;
};

template <typename T>
inline constexpr bool is_verbatim_v = is_verbatim<T>::value;

template <typename T>
concept DeaiVerbatim = is_verbatim_v<T>;

template <typename T>
concept DeaiConvertibleOnly = DeaiConvertible<T> && !DeaiVerbatim<T>;

template <>
struct deai_typeof<void> {
	static constexpr auto value = c_api::di_type::NIL;
};
template <>
struct deai_typeof<std::monostate> {
	static constexpr auto value = c_api::di_type::NIL;
};
template <>
struct deai_typeof<int> {
	static constexpr auto value = c_api::di_type::NINT;
};
template <>
struct deai_typeof<unsigned int> {
	static constexpr auto value = c_api::di_type::NUINT;
};
template <>
struct deai_typeof<int64_t> {
	static constexpr auto value = c_api::di_type::INT;
};
template <>
struct deai_typeof<uint64_t> {
	static constexpr auto value = c_api::di_type::UINT;
};
template <>
struct deai_typeof<double> {
	static constexpr auto value = c_api::di_type::FLOAT;
};
template <>
struct deai_typeof<bool> {
	static constexpr auto value = c_api::di_type::BOOL;
};
template <>
struct deai_typeof<void *> {
	static constexpr auto value = c_api::di_type::POINTER;
};
template <typename T>
    requires std::derived_from<T, type::Object>
struct deai_typeof<type::Ref<T>> {
	static constexpr auto value = c_api::di_type::OBJECT;
};
template <>
struct deai_typeof<c_api::di_object *> {
	static constexpr auto value = c_api::di_type::OBJECT;
};
template <typename T>
struct deai_typeof<WeakRef<T>> {
	static constexpr auto value = c_api::di_type::WEAK_OBJECT;
};
template <>
struct deai_typeof<c_api::di_weak_object *> {
	static constexpr auto value = c_api::di_type::WEAK_OBJECT;
};
template <>
struct deai_typeof<std::string> {
	static constexpr auto value = c_api::di_type::STRING;
};
template <>
struct deai_typeof<const char *> {
	static constexpr auto value = c_api::di_type::STRING_LITERAL;
};
template <>
struct deai_typeof<std::string_view> {
	static constexpr auto value = c_api::di_type::STRING;
};
template <>
struct deai_typeof<c_api::di_string> {
	static constexpr auto value = c_api::di_type::STRING;
};
template <>
struct deai_typeof<c_api::di_array> {
	static constexpr auto value = c_api::di_type::ARRAY;
};
template <typename T, size_t length>
    requires(is_basic_deai_type(deai_typeof<T>::value))
struct deai_typeof<std::array<T, length>> {
	static constexpr auto value = c_api::di_type::ARRAY;
};
template <>
struct deai_typeof<c_api::di_tuple> {
	static constexpr auto value = c_api::di_type::TUPLE;
};
template <>
struct deai_typeof<c_api::di_variant> {
	static constexpr auto value = c_api::di_type::VARIANT;
};
template <>
struct deai_typeof<Variant> {
	static constexpr auto value = c_api::di_type::VARIANT;
};

template <DeaiVerbatim T>
struct deai_typeof<std::span<T>> {
	static constexpr auto value = c_api::di_type::ARRAY;
};

template <DeaiConvertible T>
struct deai_typeof<std::vector<T>> {
	static constexpr auto value = c_api::di_type::ARRAY;
};

static_assert(deai_typeof<Ref<Object>>::value == c_api::di_type::OBJECT);

template <>
struct is_verbatim<c_api::di_string> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<c_api::di_array> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<c_api::di_tuple> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<c_api::di_variant> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<c_api::di_object *> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<c_api::di_weak_object *> {
	static constexpr bool value = true;
};

template <>
struct deai_ctype<c_api::di_type::INT> {
	using type = int64_t;
};

template <>
struct deai_ctype<c_api::di_type::UINT> {
	using type = uint64_t;
};

template <>
struct deai_ctype<c_api::di_type::NINT> {
	using type = int;
};

template <>
struct deai_ctype<c_api::di_type::NUINT> {
	using type = unsigned int;
};

template <>
struct deai_ctype<c_api::di_type::FLOAT> {
	using type = double;
};

template <>
struct deai_ctype<c_api::di_type::BOOL> {
	using type = bool;
};

template <>
struct deai_ctype<c_api::di_type::STRING_LITERAL> {
	using type = const char *;
};

template <>
struct deai_ctype<c_api::di_type::STRING> {
	using type = c_api::di_string;
};

template <>
struct deai_ctype<c_api::di_type::POINTER> {
	using type = void *;
};

template <>
struct deai_ctype<c_api::di_type::OBJECT> {
	using type = c_api::di_object *;
};

template <>
struct deai_ctype<c_api::di_type::WEAK_OBJECT> {
	using type = c_api::di_weak_object *;
};

template <>
struct deai_ctype<c_api::di_type::ARRAY> {
	using type = c_api::di_array;
};

template <>
struct deai_ctype<c_api::di_type::TUPLE> {
	using type = c_api::di_tuple;
};

template <>
struct deai_ctype<c_api::di_type::VARIANT> {
	using type = c_api::di_variant;
};

template <c_api::di_type type>
using deai_ctype_t = typename deai_ctype<type>::type;

template <typename... Args>
constexpr auto get_deai_types() {
	return std::array<c_api::di_type, sizeof...(Args)>{
	    id::deai_typeof<std::remove_cvref_t<Args>>::value...};
}
template <typename T, c_api::di_type Type = id::deai_typeof<T>::value>
static constexpr bool is_trivially_convertible =
    id::is_basic_deai_type(Type) && (Type != c_api::di_type::OBJECT) &&
    (Type != c_api::di_type::NIL) && (Type != c_api::di_type::ANY) &&
    (Type != c_api::di_type::DI_LAST_TYPE) && (Type != c_api::di_type::WEAK_OBJECT);
template <typename T>
concept TriviallyConvertible = is_trivially_convertible<T>;
}        // namespace id
}        // namespace deai::type
