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
namespace deai {
namespace c_api {
extern "C" {
#define __auto_type auto        // NOLINT
#include "../deai.h"
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
namespace util {
inline auto string_to_borrowed_deai_value(const std::string &str) {
	return c_api::di_string{str.c_str(), str.size()};
}

inline auto string_to_borrowed_deai_value(const c_api::di_string &str) {
	return str;
}

inline auto string_to_borrowed_deai_value(const std::string_view &str) {
	return c_api::di_string{str.data(), str.size()};
}
}        // namespace util

namespace type {

template <typename T, typename = void>
struct Ref;
struct Object;
template <typename T, typename = void>
struct deai_typeof {};

constexpr auto is_basic_deai_type(c_api::di_type_t type) -> bool {
	return (type != c_api::DI_TYPE_ARRAY) && (type != c_api::DI_TYPE_TUPLE) &&
	       (type != c_api::DI_TYPE_VARIANT);
}

template <>
struct deai_typeof<void> {
	static constexpr auto value = c_api::DI_TYPE_NIL;
};
template <>
struct deai_typeof<std::monostate> {
	static constexpr auto value = c_api::DI_TYPE_NIL;
};
template <>
struct deai_typeof<int> {
	static constexpr auto value = c_api::DI_TYPE_NINT;
};
template <>
struct deai_typeof<unsigned int> {
	static constexpr auto value = c_api::DI_TYPE_NUINT;
};
template <>
struct deai_typeof<int64_t> {
	static constexpr auto value = c_api::DI_TYPE_INT;
};
template <>
struct deai_typeof<uint64_t> {
	static constexpr auto value = c_api::DI_TYPE_UINT;
};
template <>
struct deai_typeof<double> {
	static constexpr auto value = c_api::DI_TYPE_FLOAT;
};
template <>
struct deai_typeof<bool> {
	static constexpr auto value = c_api::DI_TYPE_BOOL;
};
template <>
struct deai_typeof<void *> {
	static constexpr auto value = c_api::DI_TYPE_BOOL;
};
template <typename T>
struct deai_typeof<Ref<T>, std::enable_if_t<std::is_base_of_v<Object, T>, void>> {
	static constexpr auto value = c_api::DI_TYPE_OBJECT;
};
template <>
struct deai_typeof<std::string> {
	static constexpr auto value = c_api::DI_TYPE_STRING;
};
template <>
struct deai_typeof<const char *> {
	// const char * can be strings too, we are just hijacking this type to mean
	// string literal.
	static constexpr auto value = c_api::DI_TYPE_STRING_LITERAL;
};
template <>
struct deai_typeof<std::string_view> {
	static constexpr auto value = c_api::DI_TYPE_STRING;
};
template <>
struct deai_typeof<c_api::di_array> {
	static constexpr auto value = c_api::DI_TYPE_ARRAY;
};
template <typename T, size_t length>
struct deai_typeof<std::array<T, length>, std::enable_if_t<is_basic_deai_type(deai_typeof<T>::value), void>> {
	static constexpr auto value = c_api::DI_TYPE_ARRAY;
};
template <typename T>
struct deai_typeof<std::vector<T>, std::enable_if_t<is_basic_deai_type(deai_typeof<T>::value), void>> {
	static constexpr auto value = c_api::DI_TYPE_ARRAY;
};
template <>
struct deai_typeof<c_api::di_tuple> {
	static constexpr auto value = c_api::DI_TYPE_TUPLE;
};
template <>
struct deai_typeof<c_api::di_variant> {
	static constexpr auto value = c_api::DI_TYPE_VARIANT;
};
struct Variant;

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
	c_api::di_type type_;
	c_api::di_value value;

public:
	~Variant() {
		c_api::di_free_value(type_, &value);
	}

	/// Takes ownership of `value_`
	Variant(c_api::di_type &&type_, c_api::di_value &&value_);

	/// Takes ownership of `var`
	Variant(c_api::di_variant &var);

	auto operator=(const Variant &other);

	auto operator=(Variant &&other) noexcept;

	Variant(const Variant &other);

	Variant(Variant &&other) noexcept;

	[[nodiscard]] auto type() const -> c_api::di_type_t;

	[[nodiscard]] auto raw_value() const -> const c_api::di_value &;

	[[nodiscard]] auto raw_value() -> c_api::di_value &;

	static auto nil() -> Variant;

	operator Ref<Object>();

	// Conversion for all the by-value types
	template <typename T, c_api::di_type_t type = deai_typeof<T>::value>
	auto to() const
	    -> std::enable_if_t<is_basic_deai_type(type) && type != c_api::DI_TYPE_OBJECT &&
	                            type != c_api::DI_TYPE_NIL && type != c_api::DI_TYPE_ANY &&
	                            type != c_api::DI_LAST_TYPE && type != c_api::DI_TYPE_STRING,
	                        std::optional<T>> {
		if (type != type_) {
			return std::nullopt;
		}
		if constexpr (type == c_api::DI_TYPE_BOOL) {
			return value.bool_;
		}
		if constexpr (type == c_api::DI_TYPE_INT) {
			return value.int_;
		}
		if constexpr (type == c_api::DI_TYPE_UINT) {
			return value.uint;
		}
		if constexpr (type == c_api::DI_TYPE_NINT) {
			return value.nint;
		}
		if constexpr (type == c_api::DI_TYPE_NUINT) {
			return value.nuint;
		}
		if constexpr (type == c_api::DI_TYPE_FLOAT) {
			return value.float_;
		}
		if constexpr (type == c_api::DI_TYPE_STRING_LITERAL) {
			return std::string_view(value.string_literal,
			                        strlen(value.string_literal));
		}
		if constexpr (type == c_api::DI_TYPE_POINTER) {
			return value.pointer;
		}
		unreachable();
	}

	template <typename T, c_api::di_type_t type = deai_typeof<T>::value>
	operator T() {
		return to<T>().value();
	}

	/// Extract an object ref out of this variant. If the variant contains
	/// an object ref, it would be moved out and returned. Otherwise nothing happens
	/// and nullopt is returned.
	auto object_ref() && -> std::optional<Ref<Object>>;
	/// Get an object ref out of this variant. The value is copied.
	auto object_ref() & -> std::optional<Ref<Object>>;

	template <typename T, c_api::di_type_t type = deai_typeof<T>::value>
	[[nodiscard]] auto is() const -> bool {
		return type_ == type;
	}
};
struct ObjectMembersRawGetter;

template <bool raw_>
struct ObjectMemberProxy {
protected:
	c_api::di_object *const target;
	const std::string &key;
	static constexpr bool raw = raw_;
	template <typename, typename>
	friend struct Ref;
	friend struct ObjectMembersRawGetter;
	ObjectMemberProxy(c_api::di_object *target_, const std::string &key_)
	    : target{target_}, key{key_} {
	}

public:
	/// Remove this member from the object
	void erase() const {
		if constexpr (raw) {
			c_api::di_remove_member_raw(
			    target, util::string_to_borrowed_deai_value(key));
		} else {
			c_api::di_remove_member(target,
			                        util::string_to_borrowed_deai_value(key));
		}
	}

	operator std::optional<Variant>() const {
		c_api::di_type_t type;
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

	auto operator=(const std::optional<Variant> &new_value) const -> void {
		if constexpr (raw) {
			erase();
			if (new_value.has_value()) {
				int unused rc = c_api::di_add_member_clone(
				    target, util::string_to_borrowed_deai_value(key),
				    new_value->type(), &new_value->raw_value());
				assert(rc == 0);
			}
		} else {
			if (!new_value.has_value()) {
				return erase();
			}
			exception::throw_deai_error(c_api::di_setx(
			    target, util::string_to_borrowed_deai_value(key),
			    new_value->type(), &new_value->raw_value()));
		}
	}

	// Move set only available to raw proxy
	template <bool raw2 = raw_>
	auto operator=(std::optional<Variant> &&new_value) const
	    -> std::enable_if_t<raw2 && raw2 == raw_, void> {
		erase();

		auto moved = std::move(new_value);
		if (moved.has_value()) {
			auto type = moved->type();
			exception::throw_deai_error(c_api::di_add_member_move(
			    target, util::string_to_borrowed_deai_value(key), &type,
			    &moved->raw_value()));
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
	auto operator[](const std::string &key) -> ObjectMemberProxy<true>;
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
};

template <typename T>
struct WeakRef : public WeakRefBase {
	auto upgrade() const -> std::optional<Ref<T>> {
		c_api::di_object *obj = c_api::di_upgrade_weak_ref(inner.get());
		return Ref<T>{{obj}};
	}
};

/// A reference to the generic di_object. Inherit this class to define references to more
/// specific objects. You should define a `type` for the type name in the derived class.
/// Optionally you can also define "create", if your object can be created directly.
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

	/// Create an owning Object reference from an owning c_api
	/// object reference.
	Ref(c_api::di_object *obj) : inner{T::unsafe_ref(obj)} {
		T *ptr = nullptr;
		if (!raw_check_type(raw(), ptr)) {
			throw std::invalid_argument("trying to create Ref with wrong "
			                            "kind of object");
		}
	}

	/// Take ownership of a di_object, and create a ObjectRef
	static auto ref(c_api::di_object *obj) -> std::optional<Ref<T>> {
		T *ptr = nullptr;
		if (!raw_check_type(obj, ptr)) {
			return std::nullopt;
		}
		return {T{obj}};
	}

	template <typename Other, std::enable_if_t<std::is_base_of_v<T, Other>, int> = 0>
	auto downcast() && -> std::optional<Ref<Other>> {
		if (c_api::di_check_type(raw(), Other::type)) {
			return Ref<Other>{inner.inner.release()};
		}
		return std::nullopt;
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

	[[nodiscard]] auto downgrade() const -> WeakRef<T> {
		return WeakRef<T>{{inner.get()}};
	}

	/// Listen to signal on this object

	template <typename Other>
	auto on(const std::string &signal, const Ref<Other> &handler)
	    -> std::enable_if_t<std::is_base_of_v<Object, Other>, Ref<ListenHandle>> {
		return {c_api::di_listen_to(raw(), signal.c_str(), handler.inner.get())};
	}

	auto operator[](const std::string &key) -> ObjectMemberProxy<false> {
		return {raw(), key};
	}
};        // namespace type

struct ListenHandle : Object {};

}        // namespace type

using namespace type;

namespace util {

/// Concatenate two std::arrays
template <typename Element, size_t length1, size_t length2>
constexpr auto array_cat(std::array<Element, length1> a, std::array<Element, length2> b)
    -> std::array<Element, length1 + length2> {
	std::array<Element, length1 + length2> output;
	std::copy(a.begin(), a.end(), output.begin());
	std::copy(b.begin(), b.end(), output.begin() + length1);
	return output;
}

template <typename... Args>
constexpr auto get_deai_types() {
	return std::array{deai_typeof<Args>::value...};
}

template <typename T, size_t length, c_api::di_type_t type = deai_typeof<T>::value>
auto array_to_borrowed_deai_value(const std::array<T, length> &arr)
    -> std::enable_if<is_basic_deai_type(type), c_api::di_array> {
	return c_api::di_array{length, arr.data(), type};
}

template <typename T, c_api::di_type_t type = deai_typeof<T>::value>
auto array_to_borrowed_deai_value(const std::vector<T> &arr)
    -> std ::enable_if<is_basic_deai_type(type), c_api::di_array> {
	return c_api::di_array{arr.size(), arr.data(), type};
}

inline auto array_to_borrowed_deai_value(const c_api::di_array &arr) {
	return arr;
}

template <typename T, int type = deai_typeof<T>::value>
auto to_borrowed_deai_value(const T &input) {
	if constexpr (type == c_api::DI_TYPE_OBJECT) {
		return input.raw();
	} else if constexpr (type == c_api::DI_TYPE_ARRAY) {
		return array_to_borrowed_deai_value(input);
	} else if constexpr (type == c_api::DI_TYPE_STRING) {
		return string_to_borrowed_deai_value(input);
	} else {
		return input;
	}
}

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

template <typename Return, typename... Args>
auto call_deai_method_raw(c_api::di_object *raw_ref, const std::string_view &method_name,
                          const Args &...args)
    -> std::enable_if_t<(deai_typeof<Return>::value, true), Return> {
	constexpr auto types = get_deai_types<Args...>();
	auto values = to_borrowed_deai_values(args...);
	std::array<c_api::di_variant, sizeof...(Args)> vars;
	c_api::di_tuple di_args;
	di_args.length = sizeof...(Args);
	di_args.elements = vars.data();
	for (size_t i = 0; i < sizeof...(Args); i++) {
		di_args.elements[i].value = &values[i];
		di_args.elements[i].type = types[i];
	}

	c_api::di_type_t return_type;
	c_api::di_value return_value;
	bool called;
	exception::throw_deai_error(
	    c_api::di_rawcallxn(raw_ref, util::string_to_borrowed_deai_value(method_name),
	                        &return_type, &return_value, di_args, &called));
	if (!called) {
		throw std::out_of_range("method doesn't exist");
	}

	// NOLINTNEXTLINE(performance-move-const-arg)
	return static_cast<Return>(Variant{std::move(return_type), std::move(return_value)});
}

template <typename Return, typename T, typename... Args>
auto call_deai_method(const Ref<T> &object_ref, const std::string_view &method_name,
                      const Args &...args)
    -> std::enable_if_t<(deai_typeof<Return>::value, true), Return> {
	return call_deai_method_raw<Return>(object_ref.raw(), method_name, args...);
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
	auto obj = Ref<Object>{c_api::di_new_object_with_type_name(
	    object_allocation_info<T>::offset + sizeof(T),
	    object_allocation_info<T>::alignment, T::type)};

	// Call C++ destructor on object destruction
	obj.set_raw_dtor(call_cpp_dtor_for_object<T>);

	// Call constructor
	new (reinterpret_cast<std::byte *>(obj.raw()) + object_allocation_info<T>::offset)
	    T(std::forward<Args>(args)...);
	return obj;
}

}        // namespace util

}        // namespace deai

#define DEAI_CPP_PLUGIN_ENTRY_POINT(arg)                                                       \
	/* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                       \
	static auto di_cpp_plugin_init(::deai::Ref<::deai::Core> &&arg)->int;                  \
	extern "C" visibility_default auto di_plugin_init(::deai::c_api::di_object *di)->int { \
		return di_cpp_plugin_init(                                                     \
		    ::deai::Ref<::deai::Core>{::deai::c_api::di_ref_object(di)});              \
	}                                                                                      \
	/* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                       \
	static auto di_cpp_plugin_init(::deai::Ref<::deai::Core> &&arg)->int
