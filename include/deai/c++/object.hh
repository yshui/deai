#pragma once
#include <cstdlib>
#include <cstring>
#include <format>
// #include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "c_api.hh"        // IWYU pragma: export
#include "conv.hh"
#include "typeinfo.hh"

namespace deai {

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
using ObjectBase = c_api::Object;
struct Object {
	static constexpr const char *type = "deai:object";
	ObjectBase base;
};
struct ListenHandle {
	static constexpr const char *type = "deai:ListenHandle";
	c_api::Object base;
};
}        // namespace type

namespace type {
/// Every object is an Object
inline auto raw_check_type(c_api::Object *obj, const Object * /*tag*/) -> bool {
	return true;
}

template <typeinfo::DerivedObject T>
auto raw_check_type(c_api::Object *obj, const T * /*tag*/) -> bool {
	return c_api::type::check(obj, T::type);
}

struct Variant {
	c_api::Type type;
	c_api::Value value;

	~Variant();

	/// Takes ownership of `value_`. `value_` should be discarded without being freed
	/// after this
	Variant(c_api::Type &&type_, c_api::Value &&value_);

	/// When given a plain deai value, we just memcpy it directly. The will take ownership of the value.
	/// Note this doesn't cover `di_variant`, which is covered by the specialized constructor below.
	template <typeinfo::Verbatim T, c_api::Type Type = typeinfo::of<std::remove_reference_t<T>>::value>
	Variant(T value_) : type{Type} {
		std::memcpy(&value, &value_, c_api::type::sizeof_(type));
	}

	/// Takes ownership of `var`, `var` should be discarded without being freed after
	/// this
	Variant(c_api::Variant &&var);
	Variant(const c_api::Variant &var);

	auto operator=(const Variant &other);

	auto operator=(Variant &&other) noexcept;

	Variant(const Variant &other);

	Variant(Variant &&other) noexcept;

	static auto nil() -> Variant;
	static auto bottom() -> Variant;

	operator Ref<Object>();

	operator c_api::Variant() &&;
	operator c_api::Variant() &;

	// `to -> T` and `operator std::optional<T>` are two different flavors of conversion.
	// `operator std::optional<T>` converts straight from a deai type to its corresponding
	// C++ type, no type conversions are performed otherwise. While for `to -> T`, if `T`
	// doesn't match the deai type of the variant, a conversion between deai types will be
	// attempted, before converting to the C++ type.

	template <typeinfo::Convertible T>
	auto to() && -> std::optional<T> {
		// std::cerr << "Moving to, self type " << c_api::Type_names[static_cast<int>(type)]
		//           << " target type: " << typeid(T).name() << " this: " << this << "\n";
		static constexpr c_api::Type Type = typeinfo::of<T>::value;
		/// Special casing conversion to Variant. Don't wrap another layer of Variant,
		/// instead just move from this.
		if (Type == type || Type == c_api::Type::VARIANT) {
			return std::move(*this);
		}

		std::optional<conv::to_deai_ctype<T>> c_value =
		    conv::c_api::DeaiVariantConverter<false>{std::move(value), std::move(type)};
		c_api::Value tmp{};
		c_api::Type tmp_type = Type;
		::memcpy(&tmp, &c_value.value(), c_api::type::sizeof_(Type));
		return Variant{std::move(tmp_type), std::move(tmp)};
	}

	template <typeinfo::Convertible T>
	auto to() & -> std::optional<T> {
		// std::cerr << "Copying to, self type " << c_api::Type_names[static_cast<int>(type)]
		//           << " target type: " << typeid(T).name() << " this: " << this << "\n";
		static constexpr auto Type = typeinfo::of<T>::value;
		if (Type == type || Type == c_api::Type::VARIANT) {
			return *this;
		}
		// Try to use the move conversion
		return std::move(*this).to<T>();
	}

	template <typeinfo::Verbatim T>
	operator std::optional<T>() const & {
		static constexpr auto Type = typeinfo::of<T>::value;
		if (Type != type) {
			return std::nullopt;
		}
		T value;
		::memcpy(&value, &this->value, c_api::type::sizeof_(Type));
		return value;
	}

	operator std::optional<std::string_view>() & {
		if (type == c_api::Type::STRING) {
			return {std::string_view{value.string.data, value.string.length}};
		}
		if (type == c_api::Type::STRING_LITERAL) {
			return {value.string_literal};
		}
		return std::nullopt;
	}
	operator std::optional<std::string>() const & {
		if (type == c_api::Type::STRING) {
			return {{value.string.data, value.string.length}};
		}
		if (type == c_api::Type::STRING_LITERAL) {
			return {value.string_literal};
		}
		return std::nullopt;
	}
	operator std::optional<WeakRef<Object>>() &&;
	operator std::optional<Ref<Object>>() &&;
	template <typeinfo::Convertible T>
	operator std::optional<T>() & {
		return Variant{*this};
	}
	operator std::optional<Variant>() &&;

	template <typename T>
	    requires typeinfo::Convertible<std::remove_cvref_t<T>>
	static auto from(T &&other) -> Variant {
		auto v = conv::to_owned_deai_value(std::forward<T>(other));
		return Variant{v};
	}

	/// Extract an object ref out of this variant. If the variant contains
	/// an object ref, it would be moved out and returned. Otherwise nothing happens
	/// and nullopt is returned.
	auto object_ref() && -> std::optional<Ref<Object>>;
	/// Get an object ref out of this variant. The value is copied.
	auto object_ref() & -> std::optional<Ref<Object>>;

	/// Unpack a tuple variant into an array of variants. If this variant is not
	/// a tuple, then an array with a single element is returned. The current Variant
	/// is invalidated after this operation.
	auto unpack() && -> std::vector<Variant>;

	template <typename T>
	[[nodiscard]] auto is() const -> bool {
		return type == typeinfo::of<std::remove_cvref_t<T>>::value;
	}
};
struct ObjectMembersRawGetter;

template <bool raw_>
struct ObjectMemberProxy {
protected:
	c_api::Object *const target;
	const std::string_view key;
	static constexpr bool raw = raw_;
	template <typeinfo::DerivedObject T>
	friend struct Ref;
	friend struct ObjectMembersRawGetter;
	ObjectMemberProxy(c_api::Object *target_, std::string_view key_)
	    : target{target_}, key{key_} {
	}

public:
	/// Remove this member from the object
	void erase() const;

	[[nodiscard]] auto has_value() const -> bool;

	[[nodiscard]] auto value() const -> Variant;

	operator std::optional<Variant>() const;

	auto operator->() const -> std::optional<Variant>;

	auto operator*() const -> Variant;

	auto operator=(const std::optional<Variant> &new_value) const -> const ObjectMemberProxy &;

	auto operator=(std::optional<Variant> &&new_value) const -> const ObjectMemberProxy &;
};

extern template struct ObjectMemberProxy<true>;
extern template struct ObjectMemberProxy<false>;

struct ObjectMembersRawGetter {
private:
	c_api::Object *const target;
	ObjectMembersRawGetter(c_api::Object *target_);
	template <typeinfo::DerivedObject T>
	friend struct Ref;

public:
	auto operator[](const std::string_view &key) -> ObjectMemberProxy<true>;
};

struct WeakRefDeleter {
	void operator()(c_api::WeakObject *ptr) {
		c_api::weak_object::drop(&ptr);
	}
};

struct WeakRefBase {
protected:
	std::unique_ptr<c_api::WeakObject, WeakRefDeleter> inner;

	WeakRefBase(c_api::WeakObject *ptr);
	template <typeinfo::DerivedObject T>
	friend struct Ref;

public:
	WeakRefBase(const WeakRefBase &other);
	auto operator=(const WeakRefBase &other) -> WeakRefBase &;

	auto release() && -> c_api::WeakObject *;
};

template <typeinfo::DerivedObject T>
struct WeakRef : public WeakRefBase {
	WeakRef(c_api::WeakObject *weak) : WeakRefBase{weak} {
	}
	[[nodiscard]] auto upgrade() const -> std::optional<Ref<T>> {
		c_api::Object *obj = c_api::weak_object::upgrade(inner.get());
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
template <typeinfo::DerivedObject T>
struct Ref {
private:
	struct ObjectRefDeleter {
		void operator()(T *obj) {
			::di_unref_object(&obj->base);
		}
	};
	Ref(std::unique_ptr<T, ObjectRefDeleter> &&inner) : inner{std::move(inner)} {
	}

	/// Get the raw object reference, the reference count is not changed.
	[[nodiscard]] auto raw() const -> c_api::Object * {
		return &inner->base;
	}

protected:
	std::unique_ptr<T, ObjectRefDeleter> inner;
	friend struct WeakRef<T>;

public:
	Ref(const Ref &other) = default;
	auto operator=(const Ref &other) -> Ref & = default;
	Ref(Ref &&other) noexcept = default;
	auto operator=(Ref &&other) noexcept -> Ref & = default;

	Ref(T &&obj) : inner{std::move(obj)} {
	}

	Ref(T &obj) : inner{reinterpret_cast<T *>(c_api::object::ref(&obj.base))} {
	}

	/// Create an owning Object reference from a borrowed object reference.
	/// This is the default when you create a Ref from di_object *, because usually
	/// this is used when you receive a function call, and doesn't own the object
	/// reference.
	///
	/// If you indeed want to create a Ref from a `di_object *` you DO own, use
	/// Ref::take instead.
	Ref(c_api::Object *obj) {
		if constexpr (!std::is_same_v<T, Object>) {
			T *ptr = nullptr;
			if (!raw_check_type(obj, ptr)) {
				throw std::invalid_argument("trying to create Ref with wrong "
				                            "kind of object");
			}
		}
		inner = std::unique_ptr<T, ObjectRefDeleter>{
		    reinterpret_cast<T *>(c_api::object::ref(obj))};
	}

	/// Explicitly clone this Ref.
	auto clone() const -> Ref<T> {
		return Ref<T>{std::unique_ptr<T, ObjectRefDeleter>{
		    reinterpret_cast<T *>(c_api::object::ref(&inner->base))}};
	}

	/// Take ownership of a di_object, and create a ObjectRef. The caller must own
	/// the object reference, and must not call di_unref_object on it after this.
	static auto take(c_api::Object *obj) -> std::optional<Ref<T>> {
		if constexpr (!std::is_same_v<T, Object>) {
			T *ptr = nullptr;
			if (!raw_check_type(obj, ptr)) {
				return std::nullopt;
			}
		}
		return Ref{std::unique_ptr<T, ObjectRefDeleter>{reinterpret_cast<T *>(obj)}};
	}

	template <typeinfo::DerivedObject Other>
	auto downcast() && -> std::optional<Ref<Other>> {
		if (c_api::type::check(raw(), Other::type)) {
			return Ref<Other>::take(&inner.release()->base);
		}
		return std::nullopt;
	}

	/// Upcasting to base object
	auto cast() && -> Ref<Object> {
		return *Ref<Object>::take(&inner.release()->base);
	}

	template <typename... Args>
	void emit(const std::string &signal, const Args &...args) const {
		constexpr auto types = typeinfo::get_deai_types<Args...>();
		auto values = conv::to_borrowed_deai_values(args...);
		std::array<c_api::Variant, sizeof...(Args)> vars;
		c_api::Tuple di_args;
		di_args.length = sizeof...(Args);
		di_args.elements = vars.data();
		for (size_t i = 0; i < sizeof...(Args); i++) {
			di_args.elements[i].value = &values[i];
			di_args.elements[i].type = types[i];
		}
		exception::throw_deai_error(
		    ::di_emitn(raw(), conv::string_to_borrowed_deai_value(signal), di_args));
	}

	template <typename Return, typename... Args>
	auto call(const Args &...args) const -> Return {
		constexpr auto types = typeinfo::get_deai_types<Args...>();
		auto values = conv::to_borrowed_deai_values(args...);
		std::array<c_api::Variant, sizeof...(Args)> vars;
		c_api::Tuple di_args;
		di_args.length = sizeof...(Args);
		di_args.elements = vars.data();
		for (size_t i = 0; i < sizeof...(Args); i++) {
			di_args.elements[i].value = &values[i];
			di_args.elements[i].type = types[i];
		}

		c_api::Type return_type;
		c_api::Value return_value;
		exception::throw_deai_error(
		    c_api::object::call(raw(), &return_type, &return_value, di_args));
		if constexpr (std::is_same_v<Return, void>) {
			return;
		} else {
			return Variant{std::move(return_type), std::move(return_value)}.to<Return>().value();
		}
	}

	/// Call the method `method_name` of this object
	template <typeinfo::Convertible Return, typename... Args>
	auto method_call(std::string_view method_name, const Args &...args) -> Return {
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

	auto operator->() const -> const T * {
		return inner.get();
	}
	auto operator->() -> T * {
		return inner.get();
	}

	auto as_ref() -> T & {
		return *inner.get();
	}

	/// Give up ownership of the object and return a raw di_object pointer. You will
	/// only be able to call `raw`, the destructor, or assigning to this Ref after
	/// this function. Result of calling other functions is undefined.
	auto release() && noexcept -> c_api::Object * {
		return &inner.release()->base;
	}

	[[nodiscard]] auto downgrade() const -> WeakRef<T> {
		return WeakRef<T>{{c_api::object::weakly_ref(raw())}};
	}

	/// Listen to signal on this object

	template <typeinfo::DerivedObject Other>
	auto on(const std::string_view &signal, const Ref<Other> &handler) -> Ref<ListenHandle>;

	auto operator[](const std::string_view &key) const -> ObjectMemberProxy<false> {
		return {raw(), key};
	}
};        // namespace type
template <typeinfo::DerivedObject T>
template <typeinfo::DerivedObject Other>
auto Ref<T>::on(const std::string_view &signal, const Ref<Other> &handler) -> Ref<ListenHandle> {
	return Ref<ListenHandle>::take(
	           ::di_listen_to(raw(), conv::string_to_borrowed_deai_value(signal), handler.raw()))
	    .value();
}

}        // namespace type

using namespace type;

namespace type::util {

template <typeinfo::Convertible Return, typename... Args>
auto call_raw(c_api::Object *raw_ref, const std::string_view &method_name,
              const Args &...args) -> Return {
	auto ref = Ref<Object>{raw_ref};
	return ref.method_call<Return>(method_name, args...);
}

template <typename T>
auto call_cpp_dtor_for_object(c_api::Object *obj) {
	auto *data = reinterpret_cast<T *>(obj);
	data->~T();
}

/// Create a deai object from a C++ class. The constructor and destructor of this class
/// will be called accordingly.
template <typeinfo::DerivedObject T, typename... Args>
auto new_object(Args &&...args) -> Ref<T> {
	// Allocate the object with a di_object attached to its front
	T *obj;
	if (posix_memalign(reinterpret_cast<void **>(&obj), alignof(T), sizeof(T)) != 0) {
		throw std::bad_alloc();
	}

	// Call constructor
	new (obj) T(std::forward<Args>(args)...);
	::memset(&obj->base, 0, sizeof(obj->base));
	c_api::object::init(&obj->base);

	// Call C++ destructor on object destruction
	c_api::object::set_dtor(&obj->base, call_cpp_dtor_for_object<T>);
	c_api::object::set_type(&obj->base, T::type);

	return Ref<T>::take(&obj->base).value();
}

template <typename... Args>
auto new_error(std::format_string<Args...> fmt, Args &&...args) -> Ref<Object> {
	auto err = c_api::object::new_error(std::format(fmt, std::forward<Args>(args)...).c_str());
	return *Ref<Object>::take(err);
}

template <auto func>
struct make_wrapper {
private:
	template <typename Seq, typename R, typename... Args>
	struct factory {};
	template <typename R, typename... Args, size_t... Idx>
	struct factory<std::index_sequence<Idx...>, R, Args...> {
		static constexpr c_api::Type return_type = typeinfo::of<R>::value;
		static constexpr auto nargs = sizeof...(Args);
		static constexpr auto arg_types = typeinfo::get_deai_types<Args...>();
		/// Wrap a function that takes C++ values into a function that takes deai values. Because
		/// args will go through a to_borrowed_cpp_type<to_borrowed_deai_type<T>> transformation,
		/// and have to remain passable to the original function, they have to be borrow inversible.
		static auto wrapper(conv::to_deai_ctype<Args>... args) -> conv::to_owned_deai_type<R> {
			if constexpr (typeinfo::of<R>::value == c_api::Type::NIL) {
				// Special treatment for void
				func(conv::to_borrowed_cpp_value(args)...);
			} else {
				return conv::to_owned_deai_value(func(conv::to_borrowed_cpp_value(args)...));
			}
		}
		static inline auto raw_wrapper(c_api::Object *obj, c_api::Type *ret_type,
		                               c_api::Value *ret, c_api::Tuple args) -> int {
			if (args.length != nargs) {
				return -EINVAL;
			}
			auto result =
			    wrapper(type::conv::c_api::borrow_from_variant<conv::to_deai_ctype<Args>>(
			        *args.elements[Idx].value, args.elements[Idx].type)...);
			*ret_type = typeinfo::of<R>::value;
			::memcpy(ret, &result, c_api::type::sizeof_(*ret_type));
			return 0;
		}
	};

	// Note: cannot be evaluated
	template <typename R, typename... Args>
	static constexpr auto
	    inspect(R (*)(Args...)) -> factory<std::index_sequence_for<Args...>, R, Args...>;
	using exploded = decltype(inspect(func));

public:
	static constexpr auto value = exploded::wrapper;
	static constexpr auto raw = exploded::raw_wrapper;

	struct info : decltype(inspect(func)){};
};

/// Wrap a C++ function into a di callable object.
///
/// There are also a few restriction on your function, see `make_wrapper` for more.
template <auto func>
auto to_di_callable() -> Ref<Object> {
	constexpr auto raw_function = make_wrapper<func>::raw;
	auto *callable = c_api::object::new_(sizeof(c_api::Object), alignof(c_api::Object));
	c_api::object::set_call(callable, raw_function);
	return *Ref<Object>::take(callable);
}

template <typeinfo::DerivedObject T>
struct member_function_wrapper {
private:
	template <typename R, typename... Args>
	struct factory {
		template <auto func>
		static auto wrapper_impl(c_api::Object *obj, Args &&...args) {
			auto &this_ = *reinterpret_cast<T *>(obj);
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

template <auto func, typeinfo::DerivedObject T>
auto add_method(T &obj_impl, std::string_view name) -> void {
	constexpr auto wrapped_func = member_function_wrapper<T>::template value<func>;
	auto closure = to_di_callable<wrapped_func>().release();

	c_api::Type type = c_api::Type::OBJECT;
	exception::throw_deai_error(c_api::object::add_member_move(
	    &obj_impl.base, conv::string_to_borrowed_deai_value(name), &type, &closure));
}

template <auto func, typeinfo::DerivedObject T>
auto add_method(Ref<T> &obj, std::string_view name) -> void {
	add_method<func, T>(obj.as_ref(), name);
}

}        // namespace type::util
}        // namespace deai

namespace std {
template <class CharT>
struct formatter<deai::c_api::Type, CharT> {
	template <class ParseContext>
	constexpr auto parse(ParseContext &ctx) {
		return ctx.begin();
	}

	template <class FormatContext>
	auto format(deai::c_api::Type type, FormatContext &ctx) {
		return format_to(ctx.out(), "{}", deai::c_api::type::names[static_cast<int>(type)]);
	}
};
}        // namespace std

#define DEAI_CPP_PLUGIN_ENTRY_POINT(arg)                                                 \
	/* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                     \
	static void di_cpp_plugin_init(::deai::Ref<::deai::Core> &&arg);                     \
	extern "C" visibility_default void di_plugin_init(::deai::c_api::Object *di) {       \
		return di_cpp_plugin_init(::deai::Ref<::deai::Core>{di});                        \
	}                                                                                    \
	/* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                     \
	static void di_cpp_plugin_init(::deai::Ref<::deai::Core> &&arg)
