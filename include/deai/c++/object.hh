#pragma once
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
namespace deai {
namespace c_api {
extern "C" {
#define __auto_type auto
#include <deai/deai.h>
#undef __auto_type
}
}        // namespace c_api

namespace exception {
struct OtherError : std::exception {
private:
	int errno_;
	std::string message;

public:
	[[nodiscard]] auto what() const noexcept -> const char * override {
		return message.c_str();
	}
	OtherError(int errno__) : errno_{errno__} {
		std::stringstream ss;
		ss << "deai error " << errno_;
		message = ss.str();
	}
};

void throw_deai_error(int errno_) {
	if (errno_ == 0) {
		return;
	}
	if (errno_ == -EINVAL) {
		throw std::invalid_argument("");
	}
	if (errno_ == -ENOENT) {
		throw std::out_of_range("");
	}
	throw OtherError(errno_);
}

}        // namespace exception

namespace type {
struct Variant;

struct ObjectRefDeleter {
	void operator()(c_api::di_object *obj) {
		c_api::di_unref_object(obj);
	}
};

struct ListenHandle;

template <typename T, typename = void>
struct Ref;
struct Object {
protected:
	std::unique_ptr<c_api::di_object, ObjectRefDeleter> inner;

private:
	Object(c_api::di_object *&&obj) : inner{obj} {
		obj = nullptr;
	}

	static auto unsafe_ref(c_api::di_object *&&obj) -> Object {
		// NOLINTNEXTLINE(performance-move-const-arg)
		return Object{std::move(obj)};
	}
	template <typename, typename>
	friend struct Ref;

public:
	inline static const std::string type = "deai:object";
	static auto create() -> Ref<Object>;
	auto operator=(const Object &other) -> Object & {
		inner.reset(c_api::di_ref_object(other.inner.get()));
		return *this;
	}
	Object(const Object &other) {
		*this = other;
	}

	Object(Object &&other) = default;
	auto operator=(Object &&other) -> Object& = default;
};

/// Every object is an Object
auto raw_check_type(c_api::di_object *obj, const Object * /*tag*/) -> bool {
	return true;
}

template <typename T>
auto raw_check_type(c_api::di_object *obj, const T * /*tag*/)
    -> std::enable_if_t<std::is_base_of_v<Object, T>, bool> {
	return c_api::di_check_type(obj, T::type.c_str());
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
	Variant(c_api::di_type &&type_, c_api::di_value &&value_)
	    : type_{type_}, value{value_} {
		type_ = c_api::DI_TYPE_NIL;
		value_ = {};
	}

	/// Takes ownership of `var`
	Variant(c_api::di_variant &var) : type_{var.type} {
		memcpy(&value, var.value, c_api::di_sizeof_type(type_));
		var.type = c_api::DI_TYPE_NIL;
		std::free(var.value);
		var.value = nullptr;
	}

	auto operator=(const Variant &other) {
		type_ = other.type_;
		c_api::di_copy_value(type_, &value, &other.value);
	}

	auto operator=(Variant &&other) noexcept {
		type_ = other.type_;
		value = other.value;

		other.type_ = c_api::DI_TYPE_NIL;
		other.value = {};
	}

	Variant(const Variant &other) {
		*this = other;
	}

	Variant(Variant &&other) noexcept {
		*this = std::move(other);
	}

	[[nodiscard]] auto type() const -> c_api::di_type_t {
		return type_;
	}

	[[nodiscard]] auto raw_value() const -> const c_api::di_value & {
		return value;
	}

	[[nodiscard]] auto raw_value() -> c_api::di_value & {
		return value;
	}

	static auto nil() -> Variant {
		return {c_api::DI_TYPE_NIL, {}};
	}

	operator Ref<Object>();

	/// Extract an object ref out of this variant. If the variant contains
	/// an object ref, it would be moved out and returned. Otherwise nothing happens
	/// and nullopt is returned.
	auto object_ref() && -> std::optional<Ref<Object>>;

	/// Get an object ref out of this variant. The value is copied.
	auto object_ref() & -> std::optional<Ref<Object>>;

	[[nodiscard]] auto is_object_ref() const -> bool {
		return type_ == c_api::DI_TYPE_OBJECT;
	}

	[[nodiscard]] auto is_nil() const -> bool {
		return type_ == c_api::DI_TYPE_NIL;
	}
};

/// A reference to the generic di_object. Inherit this class to define references to more
/// specific objects. You should define a `type` for the type name in the derived class.
/// Optionally you can also define "create", if your object can be created directly.
template <typename T>
struct Ref<T, std::enable_if_t<std::is_base_of_v<Object, T>, void>> {
protected:
	T inner;
	struct ObjectMembersRawGetter;

	template <bool raw_>
	struct ObjectMemberProxy {
	protected:
		Ref &target;
		const std::string &key;
		static constexpr bool raw = raw_;
		friend struct Ref;
		friend struct ObjectMembersRawGetter;
		ObjectMemberProxy(Ref &target_, const std::string &key_)
		    : target{target_}, key{key_} {
		}

	public:
		/// Remove this member from the object
		void erase() const {
			if constexpr (raw) {
				c_api::di_remove_member_raw(target.inner.get(), key.c_str());
			} else {
				c_api::di_remove_member(target.inner.get(), key.c_str());
			}
		}

		operator std::optional<Variant>() const {
			c_api::di_type_t type;
			c_api::di_value ret;
			if constexpr (raw) {
				if (c_api::di_rawgetx(target.raw(), key.c_str(), &type,
				                      &ret) != 0) {
					return std::nullopt;
				}
			} else {
				if (c_api::di_getx(target.raw(), key.c_str(), &type, &ret) != 0) {
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
					    target.inner.get(), key.c_str(),
					    new_value->type(), &new_value->raw_value());
					assert(rc == 0);
				}
			} else {
				if (!new_value.has_value()) {
					return erase();
				}
				exception::throw_deai_error(c_api::di_setx(
				    target.inner.get(), key.c_str(), new_value->type(),
				    &new_value->raw_value()));
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
				    target.inner.get(), key.c_str(), &type, &moved->raw_value()));
			}
		}
	};
	struct ObjectMembersRawGetter {
	private:
		Ref &target;
		ObjectMembersRawGetter(Ref &target_) : target{target_} {
		}
		friend struct Ref;

	public:
		auto operator[](const std::string &key) -> ObjectMemberProxy<true> {
			return {target, key};
		}
	};

public:
	Ref(const Ref &other) = default;
	auto operator=(const Ref &other) -> Ref & = default;
	Ref(Ref &&other) noexcept = default;
	auto operator=(Ref &&other) noexcept -> Ref & = default;

	/// Create an owning Object reference from an owning c_api
	/// object reference.
	Ref(T &&obj) : inner{std::move(obj)} {
	}

	// NOLINTNEXTLINE(performance-move-const-arg)
	Ref(c_api::di_object *&&obj) : inner{T::unsafe_ref(std::move(obj))} {
		T *ptr = nullptr;
		if (!raw_check_type(inner.inner.get(), ptr)) {
			throw std::invalid_argument("trying to create Ref with wrong "
			                            "kind of object");
		}
	}

	/// Take ownership of a di_object, and create a ObjectRef
	static auto ref(c_api::di_object *&&obj) -> std::optional<Ref<T>> {
		T *ptr = nullptr;
		if (!raw_check_type(obj, ptr)) {
			return std::nullopt;
		}
		return {T{obj}};
	}

	template <typename Other, std::enable_if_t<std::is_base_of_v<T, Other>, int> = 0>
	auto downcast() && -> std::optional<Ref<Other>> {
		if (c_api::di_check_type(inner.inner.get(), Other::type.c_str())) {
			return Ref<Other>{inner.inner.release()};
		}
		return std::nullopt;
	}

	/// Returns a getter with which you can get members of the object without going
	/// through the deai getters
	auto raw_members() -> ObjectMembersRawGetter {
		return {*this};
	}

	/// Get the raw object reference, the reference count is not changed.
	[[nodiscard]] auto raw() const -> c_api::di_object * {
		return inner.inner.get();
	}

	auto operator->() const -> const T * {
		return &inner;
	}

	/// Listen to signal on this object

	template <typename Other>
	auto on(const std::string &signal, const Ref<Other> &handler)
	    -> std::enable_if_t<std::is_base_of_v<Object, Other>, Ref<ListenHandle>> {
		return {c_api::di_listen_to(this->inner.get(), signal.c_str(),
		                            handler.inner.get())};
	}

	auto operator[](const std::string &key) -> ObjectMemberProxy<false> {
		return {*this, key};
	}
};        // namespace type

struct ListenHandle : Object {};

Variant::operator Ref<Object>() {
	return this->object_ref().value();
}

/// Extract an object ref out of this variant. If the variant contains
/// an object ref, it would be moved out and returned. Otherwise nothing happens
/// and nullopt is returned.
auto Variant::object_ref() && -> std::optional<Ref<Object>> {
	if (type_ == c_api::DI_TYPE_OBJECT) {
		// NOLINTNEXTLINE(performance-move-const-arg)
		auto ret = Ref<Object>{std::move(value.object)};
		type_ = c_api::DI_TYPE_NIL;
		return {ret};
	}
	return std::nullopt;
}

/// Get an object ref out of this variant. The value is copied.
auto Variant::object_ref() & -> std::optional<Ref<Object>> {
	return Variant{*this}.object_ref();
}

auto Object::create() -> Ref<Object> {
	return Ref<Object>{Object{
	    c_api::di_new_object(sizeof(c_api::di_object), alignof(c_api::di_object))}};
}

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

template <typename T, typename = void>
struct deai_typeof {};

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
	static constexpr auto value = c_api::DI_TYPE_STRING_LITERAL;
};
template <>
struct deai_typeof<c_api::di_array> {
	static constexpr auto value = c_api::DI_TYPE_ARRAY;
};
template <typename T, size_t length>
struct deai_typeof<std::array<T, length>, std::enable_if_t<!!deai_typeof<T>::value, void>> {
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

template <typename... Args>
constexpr auto get_deai_types() {
	return std::array{deai_typeof<Args>::value...};
}

template <typename T, int type = deai_typeof<T>::value>
auto to_borrowed_deai_value(const T &input) {
	if constexpr (type == c_api::DI_TYPE_OBJECT) {
		return input.raw();
	} else if constexpr (type == c_api::DI_TYPE_STRING) {
		return input.c_str();
	} else if constexpr (type == c_api::DI_TYPE_ARRAY) {
		c_api::di_array ret;
	} else {
		return input;
	}
}

template <typename T>
auto to_borrowed_deai_value_union(const T &input) -> c_api::di_value {
	c_api::di_value ret;
	auto tmp = to_borrowed_deai_value<T>(input);
	std::memcpy(&ret, &tmp, sizeof(tmp));
	return ret;
}

template <typename... Args>
auto to_borrowed_deai_values(const Args &... args)
    -> std::array<c_api::di_value, sizeof...(Args)> {
	return std::array{to_borrowed_deai_value_union(args)...};
}

template <typename Return, typename... Args>
auto call_deai_method_raw(c_api::di_object *raw_ref, const std::string &method_name,
                          const Args &... args)
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
	exception::throw_deai_error(c_api::di_rawcallxn(
	    raw_ref, method_name.c_str(), &return_type, &return_value, di_args, &called));
	if (!called) {
		throw std::out_of_range("method doesn't exist");
	}

	// NOLINTNEXTLINE(performance-move-const-arg)
	return static_cast<Return>(Variant{std::move(return_type), std::move(return_value)});
}

template <typename Return, typename T, typename... Args>
auto call_deai_method(const Ref<T> &object_ref, const std::string &method_name, const Args &... args)
    -> std::enable_if_t<(deai_typeof<Return>::value, true), Return> {
	return call_deai_method_raw<Return>(object_ref.raw(), method_name, args...);
}

}        // namespace util

}        // namespace deai

#define DEAI_CPP_PLUGIN_ENTRY_POINT(arg)                                                     \
	/* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                     \
	static auto di_cpp_plugin_init(deai::Ref<deai::Core> &&arg)->int;                    \
	extern "C" visibility_default auto di_plugin_init(deai::c_api::di_object *di)->int { \
		return di_cpp_plugin_init(                                                   \
		    deai::Ref<deai::Core>{deai::c_api::di_ref_object(di)});                  \
	}                                                                                    \
	/* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                     \
	static auto di_cpp_plugin_init(deai::Ref<deai::Core> &&arg)->int
