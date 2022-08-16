#pragma once
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#if __cplusplus > 201703L
#include <span>
namespace deai::support {
using std::span;

}        // namespace deai::support
#else
#define TCB_SPAN_NAMESPACE_NAME deai::support
#include "span.hpp"
#endif

namespace deai {
namespace support {

template <typename T>
using remove_cvref_t = typename std::remove_cv<typename std::remove_reference<T>::type>::type;

template <bool... Bools>
struct all_of;

template <>
struct all_of<> {
	static constexpr bool value = true;
};

template <bool Head, bool... Rest>
struct all_of<Head, Rest...> {
	static constexpr bool value = Head && all_of<Rest...>::value;
};

template <bool... Bools>
inline constexpr bool all_of_v = all_of<Bools...>::value;

}        // namespace support
namespace c_api {
extern "C" {
#define __auto_type auto        // NOLINT
#include "../object.h"
#include "../error.h"
#include "../callable.h"
#undef __auto_type
}
}        // namespace c_api

namespace exception {
struct OtherError : std::exception {
private:
	int errno_;
	std::string message;

public:
	[[nodiscard]] auto what() const noexcept -> const char * override;
	OtherError(int errno_);
};

void throw_deai_error(int errno_);

}        // namespace exception

namespace type {

template <typename T, typename = void>
struct Ref;
struct Object;
template <typename T>
struct WeakRef;
struct Variant;

namespace util {
template <typename T, typename = void>
struct deai_typeof {};

constexpr auto is_basic_deai_type(c_api::di_type type) -> bool {
	return (type != c_api::di_type::ARRAY) && (type != c_api::di_type::TUPLE) &&
	       (type != c_api::di_type::VARIANT);
}

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
struct deai_typeof<type::Ref<T>, std::enable_if_t<std::is_base_of_v<type::Object, T>, void>> {
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
struct deai_typeof<std::array<T, length>, std::enable_if_t<is_basic_deai_type(deai_typeof<T>::value), void>> {
	static constexpr auto value = c_api::di_type::ARRAY;
};
template <typename T>
struct deai_typeof<std::vector<T>, std::enable_if_t<is_basic_deai_type(deai_typeof<T>::value), void>> {
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

static_assert(deai_typeof<Ref<Object>>::value == c_api::di_type::OBJECT);

/// Whether T is a c_api di_* type
template <typename T, c_api::di_type type = deai_typeof<T>::value>
struct is_verbatim {
	static constexpr bool value =
	    is_basic_deai_type(type) && type != c_api::di_type::OBJECT &&
	    type != c_api::di_type::WEAK_OBJECT && type != c_api::di_type::STRING;
};

template <>
struct is_verbatim<c_api::di_string, c_api::di_type::STRING> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<c_api::di_array, c_api::di_type::ARRAY> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<c_api::di_tuple, c_api::di_type::TUPLE> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<c_api::di_variant, c_api::di_type::VARIANT> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<c_api::di_object *, c_api::di_type::OBJECT> {
	static constexpr bool value = true;
};

template <>
struct is_verbatim<c_api::di_weak_object *, c_api::di_type::WEAK_OBJECT> {
	static constexpr bool value = true;
};

template <typename T>
inline constexpr bool is_verbatim_v = is_verbatim<T>::value;

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

template <typename... Args>
constexpr auto get_deai_types() {
	return std::array<c_api::di_type, sizeof...(Args)>{
	    deai_typeof<support::remove_cvref_t<Args>>::value...};
}

template <typename T, size_t length, c_api::di_type type = deai_typeof<T>::value>
auto array_to_borrowed_deai_value(const std::array<T, length> &arr)
    -> std::enable_if<is_basic_deai_type(type), c_api::di_array> {
	return c_api::di_array{length, arr.data(), type};
}

template <typename T, c_api::di_type type = deai_typeof<T>::value>
auto array_to_borrowed_deai_value(const std::vector<T> &arr)
    -> std ::enable_if<is_basic_deai_type(type), c_api::di_array> {
	return c_api::di_array{arr.size(), arr.data(), type};
}

inline auto array_to_borrowed_deai_value(const c_api::di_array &arr) {
	return arr;
}

/// Borrow a C++ value into a deai value
template <typename T, c_api::di_type type = deai_typeof<typename std::remove_reference<T>::type>::value>
auto to_borrowed_deai_value(const T &input) {
	if constexpr (is_verbatim_v<typename std::remove_reference<T>::type>) {
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
template <typename T, c_api::di_type type = deai_typeof<typename std::remove_reference<T>::type>::value>
auto to_borrowed_deai_variant(const T &input) -> c_api::di_variant {
	c_api::di_value *value_ptr =
	    reinterpret_cast<c_api::di_value *>(::malloc(sizeof(c_api::di_value)));
	auto value = to_borrowed_deai_value(input);
	::memcpy(value_ptr, &value, sizeof(value));
	return {.value = value, .type = type};
}

inline auto string_to_owned_deai_value(std::string &&input) -> c_api::di_string {
	auto moved = std::move(input);
	return c_api::di_string_ndup(moved.c_str(), moved.size());
}

/// Convert an owned C++ value to an owned deai value. Mostly the same as the borrowed
/// case, except for strings and arrays
template <typename T, c_api::di_type type = deai_typeof<support::remove_cvref_t<T>>::value>
auto to_owned_deai_value(T &&input) {
	if constexpr (is_verbatim_v<support::remove_cvref_t<T>>) {
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
auto to_borrowed_cpp_value(T &&arg)
    -> std::enable_if_t<is_verbatim_v<typename std::remove_reference<T>::type>, T> {
	return std::forward<T>(arg);
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
	};
	return DeaiStringConvert{arg};
}

}        // namespace util

struct ObjectRefDeleter {
	void operator()(c_api::di_object *obj);
};
struct Object {
protected:
	std::unique_ptr<c_api::di_object, ObjectRefDeleter> inner;

private:
	Object(c_api::di_object *obj);

	static auto unsafe_ref(c_api::di_object *obj) -> Object;
	template <typename, typename>
	friend struct Ref;

public:
	static constexpr const char *type = "deai:object";
	static auto create() -> Ref<Object>;
	auto operator=(const Object &other) -> Object &;
	Object(const Object &other);

	Object(Object &&other) = default;
	auto operator=(Object &&other) -> Object & = default;
};

/// Every object is an Object
inline auto raw_check_type(c_api::di_object *obj, const Object * /*tag*/) -> bool {
	return true;
}

template <typename T>
auto raw_check_type(c_api::di_object *obj, const T * /*tag*/)
    -> std::enable_if_t<std::is_base_of_v<Object, T>, bool> {
	return c_api::di_check_type(obj, T::type);
}

struct Variant {
private:
	template <typename T, c_api::di_type Type = util::deai_typeof<T>::value>
	static constexpr bool
	    is_trivially_convertible = util::is_basic_deai_type(Type) &&
	                               (Type != c_api::di_type::OBJECT) &&
	                               (Type != c_api::di_type::NIL) &&
	                               (Type != c_api::di_type::ANY) &&
	                               (Type != c_api::di_type::DI_LAST_TYPE) &&
	                               (Type != c_api::di_type::WEAK_OBJECT);

public:
	c_api::di_type type;
	c_api::di_value value;

	~Variant();

	/// Takes ownership of `value_`. `value_` should be discarded without being freed
	/// after this
	Variant(c_api::di_type type_, const c_api::di_value &value_);

	/// Takes ownership of a plain deai value
	template <typename T, c_api::di_type Type = util::deai_typeof<typename std::remove_reference<T>::type>::value,
	          std::enable_if_t<util::is_verbatim_v<T>, int> = 0>
	Variant(T value_) : type{Type} {
		std::memcpy(&value, &value_, c_api::di_sizeof_type(type));
	}

	/// Takes ownership of `var`, `var` should be discarded without being freed after
	/// this
	Variant(const c_api::di_variant &var);

	auto operator=(const Variant &other);

	auto operator=(Variant &&other) noexcept;

	Variant(const Variant &other);

	Variant(Variant &&other) noexcept;

	static auto nil() -> Variant;
	static auto bottom() -> Variant;

	operator Ref<Object>();

	// Conversion for all the by-value types
	template <typename T, c_api::di_type Type = util::deai_typeof<T>::value>
	auto
	to() const & -> std::enable_if_t<Variant::is_trivially_convertible<T>, std::optional<T>> {
		if (Type != type) {
			return std::nullopt;
		}
		if constexpr (Type == c_api::di_type::BOOL) {
			return value.bool_;
		}
		if constexpr (Type == c_api::di_type::INT) {
			return value.int_;
		}
		if constexpr (Type == c_api::di_type::UINT) {
			return value.uint;
		}
		if constexpr (Type == c_api::di_type::NINT) {
			return value.nint;
		}
		if constexpr (Type == c_api::di_type::NUINT) {
			return value.nuint;
		}
		if constexpr (Type == c_api::di_type::FLOAT) {
			return value.float_;
		}
		if constexpr (Type == c_api::di_type::STRING_LITERAL) {
			return value.string_literal;
		}
		if constexpr (Type == c_api::di_type::STRING) {
			return std::string{value.string.data, value.string.length};
		}
		if constexpr (Type == c_api::di_type::POINTER) {
			return value.pointer;
		}
		unreachable();
	}

	operator c_api::di_variant() &&;
	operator c_api::di_variant() &;

	template <typename T, c_api::di_type Type = util::deai_typeof<T>::value>
	auto
	to() && -> std::enable_if_t<std::is_same_v<T, WeakRef<Object>>, std::optional<WeakRef<Object>>>;

	template <typename T, c_api::di_type Type = util::deai_typeof<T>::value>
	auto
	to() & -> std::enable_if_t<!Variant::is_trivially_convertible<T>, std::optional<T>> {
		// Try to use the move conversion
		auto copy = *this;
		return std::move(copy).to<T>();
	}

	template <typename T, c_api::di_type type = util::deai_typeof<T>::value>
	operator T() {
		return to<T>().value();
	}

	template <typename T, c_api::di_type type = util::deai_typeof<
	                          typename std::remove_cv<typename std::remove_reference<T>::type>::type>::value>
	static auto from(T &&other) -> Variant {
		auto v = util::to_owned_deai_value(std::forward<T>(other));
		return Variant{v};
	}

	/// Extract an object ref out of this variant. If the variant contains
	/// an object ref, it would be moved out and returned. Otherwise nothing happens
	/// and nullopt is returned.
	auto object_ref() && -> std::optional<Ref<Object>>;
	/// Get an object ref out of this variant. The value is copied.
	auto object_ref() & -> std::optional<Ref<Object>>;

	template <typename T, c_api::di_type Type = util::deai_typeof<T>::value>
	[[nodiscard]] auto is() const -> bool {
		return type == Type;
	}
};
struct ObjectMembersRawGetter;

template <bool raw_>
struct ObjectMemberProxy {
protected:
	c_api::di_object *const target;
	const std::string_view key;
	static constexpr bool raw = raw_;
	template <typename, typename>
	friend struct Ref;
	friend struct ObjectMembersRawGetter;
	ObjectMemberProxy(c_api::di_object *target_, std::string_view key_)
	    : target{target_}, key{key_} {
	}

public:
	/// Remove this member from the object
	void erase() const {
		if constexpr (raw) {
			c_api::di_delete_member_raw(
			    target, util::string_to_borrowed_deai_value(key));
		} else {
			c_api::di_delete_member(target,
			                        util::string_to_borrowed_deai_value(key));
		}
	}

	[[nodiscard]] auto has_value() const -> bool {
		return static_cast<std::optional<Variant>>(*this).has_value();
	}

	[[nodiscard]] auto value() const -> Variant {
		return static_cast<std::optional<Variant>>(*this).value();
	}

	operator std::optional<Variant>() const {
		c_api::di_type type;
		c_api::di_value ret;
		if constexpr (raw) {
			if (c_api::di_rawgetx(target, util::string_to_borrowed_deai_value(key),
			                      &type, &ret) != 0) {
				return std::nullopt;
			}
		} else {
			if (c_api::di_getx(target, util::string_to_borrowed_deai_value(key),
			                   &type, &ret) != 0) {
				return std::nullopt;
			}
		}

		// NOLINTNEXTLINE(performance-move-const-arg)
		return std::optional{Variant{std::move(type), std::move(ret)}};
	}

	auto operator->() const -> std::optional<Variant> {
		return *this;
	}

	auto operator*() const -> Variant {
		return *static_cast<std::optional<Variant>>(*this);
	}

	auto operator=(const std::optional<Variant> &new_value) const
	    -> const ObjectMemberProxy & {
		if constexpr (raw) {
			erase();
			if (new_value.has_value()) {
				int unused rc = c_api::di_add_member_clone(
				    target, util::string_to_borrowed_deai_value(key),
				    new_value->type, &new_value->value);
				assert(rc == 0);
			}
		} else {
			if (!new_value.has_value()) {
				erase();
			} else {
				// the setter/deleter should handle the deletion
				exception::throw_deai_error(c_api::di_setx(
				    target, util::string_to_borrowed_deai_value(key),
				    new_value->type, &new_value->value));
			}
		}
		return *this;
	}

	auto operator=(std::optional<Variant> &&new_value) const -> const ObjectMemberProxy & {
		erase();

		auto moved = std::move(new_value);
		if constexpr (raw) {
			if (moved.has_value()) {
				exception::throw_deai_error(c_api::di_add_member_move(
				    target, util::string_to_borrowed_deai_value(key),
				    &moved->type, &moved->value));
			}
			return *this;
		} else {
			return *this = moved;
		}
	}
};
struct ObjectMembersRawGetter {
private:
	c_api::di_object *const target;
	ObjectMembersRawGetter(c_api::di_object *target_);
	template <typename, typename>
	friend struct Ref;

public:
	auto operator[](const std::string_view &key) -> ObjectMemberProxy<true>;
};

/// A borrowed di_array. Because di_array is dynamically typed, it cannot be directly
/// represented
struct ArrayView {
private:
	c_api::di_array inner;

public:
	template <typename T, c_api::di_type type = util::deai_typeof<T>::value>
	auto to_span()
	    -> std::enable_if_t<util::is_verbatim_v<T>, std::optional<support::span<const T>>> {
		if (inner.elem_type != type) {
			return std::nullopt;
		}
		return {static_cast<const T *>(inner.arr), inner.length};
	}

	template <typename T, c_api::di_type type = util::deai_typeof<T>::value,
	          std::enable_if_t<util::is_verbatim_v<T>, int> = 0>
	operator support::span<const T>() {
		return to_span<T>().value();
	}
};

struct ListenHandle;

struct WeakRefDeleter {
	void operator()(c_api::di_weak_object *ptr) {
		c_api::di_drop_weak_ref(&ptr);
	}
};

struct WeakRefBase {
protected:
	std::unique_ptr<c_api::di_weak_object, WeakRefDeleter> inner;

	WeakRefBase(c_api::di_weak_object *ptr);
	template <typename, typename>
	friend struct Ref;

public:
	WeakRefBase(const WeakRefBase &other);
	auto operator=(const WeakRefBase &other) -> WeakRefBase &;

	auto release() && -> c_api::di_weak_object *;
};

template <typename T>
struct WeakRef : public WeakRefBase {
	template <std::enable_if_t<std::is_same_v<T, Object>, int> = 0>
	WeakRef(c_api::di_weak_object *weak) : WeakRefBase{weak} {
	}
	[[nodiscard]] auto upgrade() const -> std::optional<Ref<T>> {
		c_api::di_object *obj = c_api::di_upgrade_weak_ref(inner.get());
		if (obj != nullptr) {
			return Ref<T>::take(obj);
		}
		return std::nullopt;
	}
};

/// A reference to the generic di_object. Inherit this class to define references to more
/// specific objects. You should define a `type` for the type name in the derived class.
/// Optionally you can also define "create", if your object can be created directly.
///
/// This is to create C++ wrappers of di_object that were not defined in C++, if the
/// di_object already originated from C++, there is no need to use this.
template <typename T>
struct Ref<T, std::enable_if_t<std::is_base_of_v<Object, T>, void>> {
protected:
	T inner;
	friend struct WeakRef<T>;

public:
	Ref(const Ref &other) = default;
	auto operator=(const Ref &other) -> Ref & = default;
	Ref(Ref &&other) noexcept = default;
	auto operator=(Ref &&other) noexcept -> Ref & = default;

	Ref(T &&obj) : inner{std::move(obj)} {
	}

	/// Create an owning Object reference from a borrowed c_api object reference.
	/// This is the default when you create a Ref from di_object *, because usually
	/// this is used when you receive a function call, and doesn't own the object
	/// reference.
	///
	/// If you indeed want to create a Ref from a di_object * you DO own, use
	/// Ref::take instead.
	Ref(c_api::di_object *obj) : inner{T::unsafe_ref(di_ref_object(obj))} {
		T *ptr = nullptr;
		if (!raw_check_type(raw(), ptr)) {
			throw std::invalid_argument("trying to create Ref with wrong "
			                            "kind of object");
		}
	}

	/// Take ownership of a di_object, and create a ObjectRef
	static auto take(c_api::di_object *obj) -> std::optional<Ref<T>> {
		T *ptr = nullptr;
		if (!raw_check_type(obj, ptr)) {
			return std::nullopt;
		}
		return {T{obj}};
	}

	template <typename Other, std::enable_if_t<std::is_base_of_v<T, Other>, int> = 0>
	auto downcast() && -> std::optional<Ref<Other>> {
		if (c_api::di_check_type(raw(), Other::type)) {
			return Ref<Other>::take(inner.inner.release());
		}
		return std::nullopt;
	}

	template <typename... Args>
	void emit(const std::string &signal, const Args &...args) const {
		constexpr auto types = util::get_deai_types<Args...>();
		auto values = util::to_borrowed_deai_values(args...);
		std::array<c_api::di_variant, sizeof...(Args)> vars;
		c_api::di_tuple di_args;
		di_args.length = sizeof...(Args);
		di_args.elements = vars.data();
		for (size_t i = 0; i < sizeof...(Args); i++) {
			di_args.elements[i].value = &values[i];
			di_args.elements[i].type = types[i];
		}
		exception::throw_deai_error(c_api::di_emitn(
		    raw(), util::string_to_borrowed_deai_value(signal), di_args));
	}

	template <typename Return, typename... Args>
	auto call(const Args &...args) const {
		constexpr auto types = util::get_deai_types<Args...>();
		auto values = util::to_borrowed_deai_values(args...);
		std::array<c_api::di_variant, sizeof...(Args)> vars;
		c_api::di_tuple di_args;
		di_args.length = sizeof...(Args);
		di_args.elements = vars.data();
		for (size_t i = 0; i < sizeof...(Args); i++) {
			di_args.elements[i].value = &values[i];
			di_args.elements[i].type = types[i];
		}

		c_api::di_type return_type;
		c_api::di_value return_value;
		exception::throw_deai_error(
		    c_api::di_call_objectt(raw(), &return_type, &return_value, di_args));
		return static_cast<Return>(Variant{return_type, return_value});
	}

	/// Call the method `method_name` of this object
	template <typename Return, typename... Args>
	auto method_call(std::string_view method_name, const Args &...args)
	    -> std::enable_if_t<(util::deai_typeof<Return>::value, true), Return> {
		std::optional<Variant> method = (*this)[method_name];
		if (!method.has_value()) {
			throw std::out_of_range("method not found in object");
		}
		return method->object_ref().value().call<Return>(raw(), args...);
	}

	/// Returns a getter with which you can get members of the object without going
	/// through the deai getters
	auto raw_members() -> ObjectMembersRawGetter {
		return {raw()};
	}

	/// Get the raw object reference, the reference count is not changed.
	[[nodiscard]] auto raw() const -> c_api::di_object * {
		return inner.inner.get();
	}

	auto operator->() const -> const T * {
		return &inner;
	}

	auto set_raw_dtor(void (*dtor)(c_api::di_object *)) {
		c_api::di_set_object_dtor(raw(), dtor);
	}

	/// Give up ownership of the object and return a raw di_object pointer. You will
	/// only be able to call `raw`, the destructor, or assigning to this Ref after
	/// this function. Result of calling other functions is undefined.
	auto release() &&noexcept -> c_api::di_object * {
		return inner.inner.release();
	}

	[[nodiscard]] auto downgrade() const -> WeakRef<T> {
		return WeakRef<T>{{c_api::di_weakly_ref_object(raw())}};
	}

	/// Listen to signal on this object

	template <typename Other>
	auto on(const std::string_view &signal, const Ref<Other> &handler)
	    -> std::enable_if_t<std::is_base_of_v<Object, Other>, Ref<ListenHandle>>;

	auto operator[](const std::string_view &key) const -> ObjectMemberProxy<false> {
		return {raw(), key};
	}
};        // namespace type

struct ListenHandle : Object {
	static constexpr const char *type = "deai:ListenHandle";
};
template <typename T>
template <typename Other>
auto Ref<T, std::enable_if_t<std::is_base_of_v<Object, T>, void>>::on(const std::string_view &signal,
                                                                      const Ref<Other> &handler)
    -> std::enable_if_t<std::is_base_of_v<Object, Other>, Ref<ListenHandle>> {
	return Ref<ListenHandle>::take(
	           c_api::di_listen_to(raw(), util::string_to_borrowed_deai_value(signal),
	                               handler.raw()))
	    .value();
}

extern template auto
Variant::to<WeakRef<Object>, c_api::di_type::WEAK_OBJECT>() && -> std::optional<WeakRef<Object>>;

}        // namespace type

using namespace type;

namespace type::util {

template <typename Return, typename... Args>
auto call_raw(c_api::di_object *raw_ref, const std::string_view &method_name, const Args &...args)
    -> std::enable_if_t<(deai_typeof<Return>::value, true), Return> {
	auto ref = Ref<Object>{raw_ref};
	return ref.method_call<Return>(method_name, args...);
}

template <typename T>
struct object_allocation_info {
	static constexpr auto alignment = std::max(alignof(c_api::di_object), alignof(T));
	// Round up the sizeof di_object to multiple of alignment
	static constexpr auto offset =
	    (sizeof(c_api::di_object) + (alignment - 1)) & (-alignment);
};

template <typename T>
auto call_cpp_dtor_for_object(c_api::di_object *obj) {
	auto *data = reinterpret_cast<std::byte *>(obj);
	reinterpret_cast<T *>(data + object_allocation_info<T>::offset)->~T();
}

/// Create a deai object from a C++ class. The constructor and destructor of this class
/// will be called accordingly.
template <typename T, typename... Args>
auto new_object(Args &&...args) -> Ref<Object> {
	// Allocate the object with a di_object attached to its front
	auto obj = *Ref<Object>::take(c_api::di_new_object_with_type_name(
	    object_allocation_info<T>::offset + sizeof(T),
	    object_allocation_info<T>::alignment, T::type));

	// Call C++ destructor on object destruction
	obj.set_raw_dtor(call_cpp_dtor_for_object<T>);

	// Call constructor
	new (reinterpret_cast<std::byte *>(obj.raw()) + object_allocation_info<T>::offset)
	    T(std::forward<Args>(args)...);
	return obj;
}

/// Get the container object of type T. The T must have been created with new_object<T>.
/// Can be used in member function of T to get a `Ref<Object>` from `*this`, this is
/// usable in the constructor of the object too.
template <typename T>
auto unsafe_to_object_ref(T &obj) -> Ref<Object> {
	return {reinterpret_cast<c_api::di_object *>(reinterpret_cast<std::byte *>(&obj) -
	                                             object_allocation_info<T>::offset)};
}

/// Get the interior object of a deai object reference. This object reference must have
/// been created with new_object<T>, otherwise behavior is undefeind.
///
/// You MUST NEVER copy out of the returned reference! You will get an unusable object if
/// you do so.
template <typename T>
auto unsafe_to_inner(c_api::di_object *obj) -> T & {
	return *reinterpret_cast<T *>(reinterpret_cast<std::byte *>(obj) +
	                              object_allocation_info<T>::offset);
}

/// ditto
template <typename T>
auto unsafe_to_inner(const Ref<Object> &obj) -> T & {
	return *reinterpret_cast<T *>(reinterpret_cast<std::byte *>(obj.raw()) +
	                              object_allocation_info<T>::offset);
}

template <typename... Captures>
struct make_wrapper {
private:
	template <typename R, typename... Args>
	struct factory {
		/// Wrap a function that takes C++ values into a function that takes deai values. Because
		/// args will go through a to_borrowed_cpp_type<to_borrowed_deai_type<T>> transformation,
		/// and have to remain passable to the original function, they have to be borrow inversible.
		template <R (*func)(Args...)>
		static auto wrapper_impl(to_borrowed_deai_type<Args>... args)
		    -> to_owned_deai_type<R> {
			if constexpr (deai_typeof<R>::value == c_api::di_type::NIL) {
				// Special treatment for void
				func(to_borrowed_cpp_value(args)...);
			} else {
				return to_owned_deai_value(func(to_borrowed_cpp_value(args)...));
			}
		}
		static constexpr c_api::di_type return_type = deai_typeof<R>::value;
		static constexpr auto nargs = sizeof...(Args);
		static constexpr auto arg_types = get_deai_types<Args...>();
	};

	// Note: cannot be evaluated
	template <typename R, typename... Args>
	static constexpr auto inspect(R (*func)(Captures..., Args...)) -> factory<R, Args...>;

public:
	template <auto func>
	static constexpr auto value = decltype(inspect(func))::template wrapper_impl<func>;

	template <auto func>
	struct info : decltype(inspect(func)) {};
};

/// Wrap a C++ function into a di_closure. This function can have a list of captures,
/// which are cloned and stored inside the closure. This function must take arguments like
/// this: func(captures..., arguments...). Either or both of them can be empty.
///
/// There are also a few restriction on your function, see `make_wrapper` for more.
template <auto func, typename... Captures>
auto to_di_closure(const Captures &...captures) -> Ref<Object> {
	constexpr auto ncaptures = sizeof...(Captures);
	using wrapper_info = typename make_wrapper<Captures...>::template info<func>;
	constexpr auto function = make_wrapper<Captures...>::template value<func>;
	constexpr auto nargs = wrapper_info::nargs;
	c_api::di_variant elements[] = {to_borrowed_deai_variant(captures)...};
	c_api::di_tuple capture_tuple = {.length = ncaptures, .elements = elements};

	// XXX passing a C++ function pointer to C... not the best thing to do. C++/C
	// could have different ABIs in some cases. This seems to be OK currently, but ABI
	// changes could break us.
	return *Ref<Object>::take(reinterpret_cast<c_api::di_object *>(c_api::di_create_closure(
	    reinterpret_cast<void (*)()>(function), wrapper_info::return_type,
	    capture_tuple, nargs, wrapper_info::arg_types.data())));
}

template <typename T>
struct member_function_wrapper {
private:
	template <typename R, typename... Args>
	struct factory {
		template <auto func>
		static auto wrapper_impl(c_api::di_object *obj, Args &&...args) {
			auto &this_ = unsafe_to_inner<T>(obj);
			return (this_.*func)(std::forward<Args>(args)...);
		}
	};

	template <typename R, typename... Args>
	static constexpr auto inspect(R (T::*func)(Args...)) -> factory<R, Args...>;
	template <typename R, typename... Args>
	static constexpr auto inspect(R (T::*func)(Args...) const) -> factory<R, Args...>;

public:
	template <auto func>
	static constexpr auto value = decltype(inspect(func))::template wrapper_impl<func>;
};

template <auto func, typename T>
auto add_method(T &obj_impl, std::string_view name) -> void {
	constexpr auto wrapped_func = member_function_wrapper<T>::template value<func>;
	auto closure = to_di_closure<wrapped_func>().release();
	auto *object_ref_raw = reinterpret_cast<c_api::di_object *>(
	    reinterpret_cast<std::byte *>(&obj_impl) - object_allocation_info<T>::offset);

	c_api::di_type type = c_api::di_type::OBJECT;
	exception::throw_deai_error(c_api::di_add_member_move(
	    object_ref_raw, string_to_borrowed_deai_value(name), &type, &closure));
}

}        // namespace type::util

namespace _compile_time_checks {
using namespace type;
using namespace util;

/// Assign this to a variable you want to know the type of, to have the compiler reveal it
/// to you.
struct incompatible {};

template <typename... Types>
inline constexpr bool check_borrowed_type_transformations_v =
    support::all_of_v<is_verbatim_v<to_borrowed_deai_type<Types>>...>;

/// Make sure to_borrowed_deai_type does indeed produce di_* types
static_assert(check_borrowed_type_transformations_v<std::string_view, std::string, c_api::di_object *>);

template <typename... Types>
inline constexpr bool check_owned_type_transformations_v =
    support::all_of_v<is_verbatim_v<to_owned_deai_type<Types>>...>;

/// Make sure to_owned_deai_type does indeed produce di_* types
static_assert(check_owned_type_transformations_v<std::string, c_api::di_object *, Variant>);

}        // namespace _compile_time_checks

}        // namespace deai

#define DEAI_CPP_PLUGIN_ENTRY_POINT(arg)                                                       \
	/* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                       \
	static auto di_cpp_plugin_init(::deai::Ref<::deai::Core> &&arg)->int;                  \
	extern "C" visibility_default auto di_plugin_init(::deai::c_api::di_object *di)->int { \
		return di_cpp_plugin_init(::deai::Ref<::deai::Core>{di});                      \
	}                                                                                      \
	/* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                       \
	static auto di_cpp_plugin_init(::deai::Ref<::deai::Core> &&arg)->int
