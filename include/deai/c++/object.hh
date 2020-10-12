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

struct ListenHandleRef;

/// A reference to the generic di_object. Inherit this class to define references to more
/// specific objects. You should define a `type` for the type name in the derived class.
/// Optionally you can also define "create", if your object can be created directly.
struct ObjectRef {
protected:
	std::unique_ptr<c_api::di_object, ObjectRefDeleter> inner;
	struct ObjectMembersRawGetter;

	template <bool raw_>
	struct ObjectMemberProxy {
	protected:
		ObjectRef &target;
		const std::string &key;
		static constexpr bool raw = raw_;
		friend struct ObjectRef;
		friend struct ObjectMembersRawGetter;
		ObjectMemberProxy(ObjectRef &target_, const std::string &key_)
		    : target{target_}, key{key_} {
		}

	public:
		operator std::optional<Variant>() const;
		auto operator=(const std::optional<Variant> &new_value) const -> void;

		// Move set only available to raw proxy
		template <bool raw2 = raw_>
		auto operator=(std::optional<Variant> &&moved_new_value) const
		    -> std::enable_if_t<raw2 && raw2 == raw_, void>;
		auto operator->() const -> std::optional<Variant>;
		auto operator*() const -> Variant;

		/// Remove this member from the object
		void erase() const {
			if constexpr (raw) {
				c_api::di_remove_member_raw(target.inner.get(), key.c_str());
			} else {
				c_api::di_remove_member(target.inner.get(), key.c_str());
			}
		}
	};
	struct ObjectMembersRawGetter {
	private:
		ObjectRef &target;
		ObjectMembersRawGetter(ObjectRef &target_) : target{target_} {
		}
		friend struct ObjectRef;

	public:
		auto operator[](const std::string &key) -> ObjectMemberProxy<true> {
			return {target, key};
		}
	};

	/// Create an owning Object reference from an owning c_api
	/// object reference.
	ObjectRef(c_api::di_object *&&obj) : inner(obj) {
	}

public:
	inline static const std::string type = "deai:object";

	/// Create a new empty di_object
	static auto create() -> ObjectRef {
		return ObjectRef{c_api::di_new_object(sizeof(c_api::di_object),
		                                      alignof(c_api::di_object))};
	}

	/// Take ownership of a di_object, and create a ObjectRef
	static auto create(c_api::di_object *&&obj) -> ObjectRef {
		// NOLINTNEXTLINE(performance-move-const-arg)
		return ObjectRef{std::move(obj)};
	}

	auto operator=(ObjectRef &&) -> ObjectRef & = default;

	/// Clone this reference. This will increment the reference count on the
	/// inner di_object.
	auto operator=(const ObjectRef &other) -> ObjectRef & {
		*this = ObjectRef{c_api::di_ref_object(other.inner.get())};
		return *this;
	}

	ObjectRef(const ObjectRef &other) {
		*this = other;
	}

	template <typename T, std::enable_if_t<std::is_base_of_v<ObjectRef, T>, int> = 0>
	auto cast() && -> std::optional<T> {
		if (c_api::di_check_type(inner.get(), T::type.c_str())) {
			return T{inner.release()};
		}
		return std::nullopt;
	}

	/// Get the raw object reference, the reference count is not changed.
	[[nodiscard]] auto raw() const -> c_api::di_object * {
		return inner.get();
	}

	/// Returns a getter with which you can get members of the object without going
	/// through the deai getters
	auto raw_members() -> ObjectMembersRawGetter {
		return {*this};
	}

	/// Listen to signal on this object
	auto on(const std::string &signal, const ObjectRef &handler) -> ListenHandleRef;

	auto operator[](const std::string &key) -> ObjectMemberProxy<false> {
		return {*this, key};
	}
};

struct ListenHandleRef : ObjectRef {};

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

	operator ObjectRef() {
		return this->object_ref().value();
	}

	/// Extract an object ref out of this variant. If the variant contains
	/// an object ref, it would be moved out and returned. Otherwise nothing happens
	/// and nullopt is returned.
	auto object_ref() && -> std::optional<ObjectRef> {
		if (type_ == c_api::DI_TYPE_OBJECT) {
			// NOLINTNEXTLINE(performance-move-const-arg)
			auto ret = ObjectRef::create(std::move(value.object));
			type_ = c_api::DI_TYPE_NIL;
			return ret;
		}
		return std::nullopt;
	}

	/// Get an object ref out of this variant. The value is copied.
	auto object_ref() & -> std::optional<ObjectRef> {
		return Variant{*this}.object_ref();
	}

	[[nodiscard]] auto is_object_ref() const -> bool {
		return type_ == c_api::DI_TYPE_OBJECT;
	}

	[[nodiscard]] auto is_nil() const -> bool {
		return type_ == c_api::DI_TYPE_NIL;
	}
};

template <bool raw>
inline ObjectRef::ObjectMemberProxy<raw>::operator std::optional<Variant>() const {
	c_api::di_type_t type;
	c_api::di_value ret;
	if constexpr (raw) {
		if (c_api::di_rawgetx(target.inner.get(), key.c_str(), &type, &ret) != 0) {
			return std::nullopt;
		}
	} else {
		if (c_api::di_getx(target.inner.get(), key.c_str(), &type, &ret) != 0) {
			return std::nullopt;
		}
	}

	// NOLINTNEXTLINE(performance-move-const-arg)
	return std::optional{Variant{std::move(type), std::move(ret)}};
}

template <bool raw>
inline auto ObjectRef::ObjectMemberProxy<raw>::operator->() const -> std::optional<Variant> {
	return *this;
}

template <bool raw>
inline auto ObjectRef::ObjectMemberProxy<raw>::operator*() const -> Variant {
	return *static_cast<std::optional<Variant>>(*this);
}

template <bool raw_>
auto ObjectRef::ObjectMemberProxy<raw_>::operator=(const std::optional<Variant> &new_value) const
    -> void {
	if constexpr (raw) {
		erase();
		if (new_value.has_value()) {
			int unused rc = c_api::di_add_member_clone(
			    target.inner.get(), key.c_str(), new_value->type(),
			    &new_value->raw_value());
			assert(rc == 0);
		}
	} else {
		if (!new_value.has_value()) {
			return erase();
		}
		exception::throw_deai_error(c_api::di_setx(target.inner.get(),
		                                           key.c_str(), new_value->type(),
		                                           &new_value->raw_value()));
	}
}

template <bool raw_>
template <bool raw2>
auto ObjectRef::ObjectMemberProxy<raw_>::operator=(std::optional<Variant> &&new_value) const
    -> std::enable_if_t<raw2 && raw2 == raw_, void> {
	erase();

	auto moved = std::move(new_value);
	if (moved.has_value()) {
		auto type = moved->type();
		exception::throw_deai_error(c_api::di_add_member_move(
		    target.inner.get(), key.c_str(), &type, &moved->raw_value()));
	}
}

auto ObjectRef::on(const std::string &signal, const ObjectRef &handler) -> ListenHandleRef {
	return {c_api::di_listen_to(inner.get(), signal.c_str(), handler.inner.get())};
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
struct deai_typeof;

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
struct deai_typeof<T, std::enable_if_t<std::is_base_of_v<ObjectRef, T>, void>> {
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

template <typename Return, typename T, typename... Args>
auto call_deai_method(const T &object_ref, const std::string &method_name, const Args &... args)
    -> std::enable_if_t<std::is_base_of_v<ObjectRef, T>, Return> {
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
	exception::throw_deai_error(c_api::di_rawcallxn(object_ref.raw(),
	                                                method_name.c_str(), &return_type,
	                                                &return_value, di_args, &called));
	if (!called) {
		throw std::out_of_range("method doesn't exist");
	}

	// NOLINTNEXTLINE(performance-move-const-arg)
	return static_cast<Return>(Variant{std::move(return_type), std::move(return_value)});
}
}        // namespace util

}        // namespace deai

#define DEAI_CPP_PLUGIN_ENTRY_POINT(arg)                                                          \
	/* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                          \
	static auto di_cpp_plugin_init(deai::CoreRef &&arg)->int;                                 \
	extern "C" visibility_default auto di_plugin_init(deai::c_api::di_object *di)->int {      \
		return di_cpp_plugin_init(deai::ObjectRef::create(deai::c_api::di_ref_object(di)) \
		                              .cast<deai::CoreRef>()                              \
		                              .value());                                          \
	}                                                                                         \
	/* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                          \
	static auto di_cpp_plugin_init(deai::CoreRef &&arg)->int
