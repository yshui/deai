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
struct ObjectRefDeleter {
	void operator()(c_api::di_object *obj);
};
struct Object {
	protected:
	std::unique_ptr<c_api::di_object, ObjectRefDeleter> inner;

	private:
	Object(c_api::di_object *obj);

	static auto unsafe_ref(c_api::di_object *obj) -> Object;
	template <typename T>
	    requires std::derived_from<T, Object>
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
    requires std::derived_from<T, Object>
auto raw_check_type(c_api::di_object *obj, const T * /*tag*/) -> bool {
	return c_api::di_check_type(obj, T::type);
}

struct Variant {
	c_api::di_type type;
	c_api::di_value value;

	~Variant();

	/// Takes ownership of `value_`. `value_` should be discarded without being freed
	/// after this
	Variant(c_api::di_type &&type_, c_api::di_value &&value_);

	/// When given a plain deai value, we just memcpy it directly. The will take ownership of the value.
	/// Note this doesn't cover `di_variant`, which is covered by the specialized constructor below.
	template <id::DeaiVerbatim T, c_api::di_type Type = id::deai_typeof<std::remove_reference_t<T>>::value>
	Variant(T value_) : type{Type} {
		std::memcpy(&value, &value_, c_api::di_sizeof_type(type));
	}

	/// Takes ownership of `var`, `var` should be discarded without being freed after
	/// this
	Variant(c_api::di_variant &&var);
	Variant(const c_api::di_variant &var);

	auto operator=(const Variant &other);

	auto operator=(Variant &&other) noexcept;

	Variant(const Variant &other);

	Variant(Variant &&other) noexcept;

	static auto nil() -> Variant;
	static auto bottom() -> Variant;

	operator Ref<Object>();

	operator c_api::di_variant() &&;
	operator c_api::di_variant() &;

	// `to -> T` and `operator std::optional<T>` are two different flavors of conversion.
	// `operator std::optional<T>` converts straight from a deai type to its corresponding
	// C++ type, no type conversions are performed otherwise. While for `to -> T`, if `T`
	// doesn't match the deai type of the variant, a conversion between deai types will be
	// attempted, before converting to the C++ type.

	template <id::DeaiConvertible T>
	auto to() && -> std::optional<T> {
		// std::cerr << "Moving to, self type " << c_api::di_type_names[static_cast<int>(type)]
		//           << " target type: " << typeid(T).name() << " this: " << this << "\n";
		static constexpr c_api::di_type Type = id::deai_typeof<T>::value;
		/// Special casing conversion to Variant. Don't wrap another layer of Variant,
		/// instead just move from this.
		if (Type == type || Type == c_api::di_type::VARIANT) {
			return std::move(*this);
		}

		std::optional<id::deai_ctype_t<Type>> c_value =
		    conv::c_api::DeaiVariantConverter<false>{std::move(value), std::move(type)};
		c_api::di_value tmp{};
		c_api::di_type tmp_type = Type;
		::memcpy(&tmp, &c_value.value(), c_api::di_sizeof_type(Type));
		return Variant{std::move(tmp_type), std::move(tmp)};
	}

	template <id::DeaiConvertible T>
	auto to() & -> std::optional<T> {
		// std::cerr << "Copying to, self type " << c_api::di_type_names[static_cast<int>(type)]
		//           << " target type: " << typeid(T).name() << " this: " << this << "\n";
		static constexpr c_api::di_type Type = id::deai_typeof<T>::value;
		if (Type == type || Type == c_api::di_type::VARIANT) {
			return *this;
		}
		// Try to use the move conversion
		return std::move(*this).to<T>();
	}

	template <id::DeaiVerbatim T>
	operator std::optional<T>() const & {
		static constexpr c_api::di_type Type = id::deai_typeof<T>::value;
		if (Type != type) {
			return std::nullopt;
		}
		T value;
		::memcpy(&value, &this->value, c_api::di_sizeof_type(Type));
		return value;
	}

	operator std::optional<std::string_view>() & {
		if (type == c_api::di_type::STRING) {
			return {std::string_view{value.string.data, value.string.length}};
		}
		if (type == c_api::di_type::STRING_LITERAL) {
			return {value.string_literal};
		}
		return std::nullopt;
	}
	operator std::optional<std::string>() const & {
		if (type == c_api::di_type::STRING) {
			return {{value.string.data, value.string.length}};
		}
		if (type == c_api::di_type::STRING_LITERAL) {
			return {value.string_literal};
		}
		return std::nullopt;
	}
	operator std::optional<WeakRef<Object>>() &&;
	operator std::optional<Ref<Object>>() &&;
	template <id::DeaiConvertible T>
	operator std::optional<T>() & {
		return Variant{*this};
	}
	operator std::optional<Variant>() &&;

	template <typename T, c_api::di_type type = id::deai_typeof<std::remove_cv_t<std::remove_reference_t<T>>>::value>
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

	template <typename T, c_api::di_type Type = id::deai_typeof<T>::value>
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
	template <typename T>
	    requires std::derived_from<T, Object>
	friend struct Ref;
	friend struct ObjectMembersRawGetter;
	ObjectMemberProxy(c_api::di_object *target_, std::string_view key_)
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
	c_api::di_object *const target;
	ObjectMembersRawGetter(c_api::di_object *target_);
	template <typename T>
	    requires std::derived_from<T, Object>
	friend struct Ref;

	public:
	auto operator[](const std::string_view &key) -> ObjectMemberProxy<true>;
};

struct ListenHandle : public Object {
	static constexpr const char *type = "deai:ListenHandle";
};

struct WeakRefDeleter {
	void operator()(c_api::di_weak_object *ptr) {
		c_api::di_drop_weak_ref(&ptr);
	}
};

struct WeakRefBase {
	protected:
	std::unique_ptr<c_api::di_weak_object, WeakRefDeleter> inner;

	WeakRefBase(c_api::di_weak_object *ptr);
	template <typename T>
	    requires std::derived_from<T, Object>
	friend struct Ref;

	public:
	WeakRefBase(const WeakRefBase &other);
	auto operator=(const WeakRefBase &other) -> WeakRefBase &;

	auto release() && -> c_api::di_weak_object *;
};

template <typename T>
struct WeakRef : public WeakRefBase {
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
    requires std::derived_from<T, Object>
struct Ref {
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

	template <typename Other>
	    requires std::derived_from<Other, T>
	auto downcast() && -> std::optional<Ref<Other>> {
		if (c_api::di_check_type(raw(), Other::type)) {
			return Ref<Other>::take(inner.inner.release());
		}
		return std::nullopt;
	}

	template <typename... Args>
	void emit(const std::string &signal, const Args &...args) const {
		constexpr auto types = id::get_deai_types<Args...>();
		auto values = conv::to_borrowed_deai_values(args...);
		std::array<c_api::di_variant, sizeof...(Args)> vars;
		c_api::di_tuple di_args;
		di_args.length = sizeof...(Args);
		di_args.elements = vars.data();
		for (size_t i = 0; i < sizeof...(Args); i++) {
			di_args.elements[i].value = &values[i];
			di_args.elements[i].type = types[i];
		}
		exception::throw_deai_error(
		    c_api::di_emitn(raw(), conv::string_to_borrowed_deai_value(signal), di_args));
	}

	template <typename Return, typename... Args>
	auto call(const Args &...args) const -> Return {
		constexpr auto types = id::get_deai_types<Args...>();
		auto values = conv::to_borrowed_deai_values(args...);
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
		if constexpr (std::is_same_v<Return, void>) {
			return;
		} else {
			return Variant{std::move(return_type), std::move(return_value)}.to<Return>().value();
		}
	}

	/// Call the method `method_name` of this object
	template <id::DeaiConvertible Return, typename... Args>
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
	auto release() && noexcept -> c_api::di_object * {
		return inner.inner.release();
	}

	[[nodiscard]] auto downgrade() const -> WeakRef<T> {
		return WeakRef<T>{{c_api::di_weakly_ref_object(raw())}};
	}

	/// Listen to signal on this object

	template <typename Other>
	    requires std::is_base_of_v<Other, Object>
	auto on(const std::string_view &signal, const Ref<Other> &handler) -> Ref<ListenHandle>;

	auto operator[](const std::string_view &key) const -> ObjectMemberProxy<false> {
		return {raw(), key};
	}
};        // namespace type
template <typename T>
    requires std::derived_from<T, Object>
template <typename Other>
    requires std::is_base_of_v<Other, Object>
auto Ref<T>::on(const std::string_view &signal, const Ref<Other> &handler)
    -> Ref<ListenHandle> {
	return Ref<ListenHandle>::take(
	           c_api::di_listen_to(raw(), conv::string_to_borrowed_deai_value(signal),
	                               handler.raw()))
	    .value();
}

}        // namespace type

using namespace type;

namespace type::util {

template <id::DeaiConvertible Return, typename... Args>
auto call_raw(c_api::di_object *raw_ref, const std::string_view &method_name,
              const Args &...args) -> Return {
	auto ref = Ref<Object>{raw_ref};
	return ref.method_call<Return>(method_name, args...);
}

template <typename T>
struct object_allocation_info {
	static constexpr auto alignment = std::max(alignof(c_api::di_object), alignof(T));
	// Round up the sizeof di_object to multiple of alignment
	static constexpr auto offset = (sizeof(c_api::di_object) + (alignment - 1)) & (-alignment);
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

template <auto func>
struct make_wrapper {
	private:
	template <typename Seq, typename R, typename... Args>
	struct factory {};
	template <typename R, typename... Args, size_t... Idx>
	struct factory<std::index_sequence<Idx...>, R, Args...> {
		static constexpr c_api::di_type return_type = id::deai_typeof<R>::value;
		static constexpr auto nargs = sizeof...(Args);
		static constexpr auto arg_types = id::get_deai_types<Args...>();
		/// Wrap a function that takes C++ values into a function that takes deai values. Because
		/// args will go through a to_borrowed_cpp_type<to_borrowed_deai_type<T>> transformation,
		/// and have to remain passable to the original function, they have to be borrow inversible.
		static auto wrapper(conv::to_deai_ctype<Args>... args) -> conv::to_owned_deai_type<R> {
			if constexpr (id::deai_typeof<R>::value == c_api::di_type::NIL) {
				// Special treatment for void
				func(conv::to_borrowed_cpp_value(args)...);
			} else {
				return conv::to_owned_deai_value(func(conv::to_borrowed_cpp_value(args)...));
			}
		}
		static inline auto raw_wrapper(c_api::di_object *obj, c_api::di_type *ret_type,
		                               c_api::di_value *ret, c_api::di_tuple args) -> int {
			if (args.length != nargs) {
				return -EINVAL;
			}
			auto result =
			    wrapper(type::conv::c_api::borrow_from_variant<conv::to_deai_ctype<Args>>(
			        *args.elements[Idx].value, args.elements[Idx].type)...);
			*ret_type = id::deai_typeof<R>::value;
			::memcpy(ret, &result, c_api::di_sizeof_type(*ret_type));
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

	struct info : decltype(inspect(func)) {};
};

/// Wrap a C++ function into a di callable object.
///
/// There are also a few restriction on your function, see `make_wrapper` for more.
template <auto func>
auto to_di_callable() -> Ref<Object> {
	constexpr auto raw_function = make_wrapper<func>::raw;
	auto *callable = c_api::di_new_object(sizeof(c_api::di_object), alignof(c_api::di_object));
	c_api::di_set_object_call(callable, raw_function);
	return *Ref<Object>::take(callable);
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
	auto closure = to_di_callable<wrapped_func>().release();
	auto *object_ref_raw = reinterpret_cast<c_api::di_object *>(
	    reinterpret_cast<std::byte *>(&obj_impl) - object_allocation_info<T>::offset);

	c_api::di_type type = c_api::di_type::OBJECT;
	exception::throw_deai_error(c_api::di_add_member_move(
	    object_ref_raw, conv::string_to_borrowed_deai_value(name), &type, &closure));
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
    (... && id::is_verbatim_v<conv::to_borrowed_deai_type<Types>>);

/// Make sure to_borrowed_deai_type does indeed produce di_* types
static_assert(check_borrowed_type_transformations_v<std::string_view, std::string, c_api::di_object *>);

template <typename... Types>
inline constexpr bool check_owned_type_transformations_v =
    (... && id::is_verbatim_v<conv::to_owned_deai_type<Types>>);

/// Make sure to_owned_deai_type does indeed produce di_* types
static_assert(check_owned_type_transformations_v<std::string, c_api::di_object *, Variant>);

}        // namespace _compile_time_checks

}        // namespace deai

namespace std {

template <class CharT>
struct formatter<deai::c_api::di_type, CharT> {
	template <class ParseContext>
	constexpr auto parse(ParseContext &ctx) {
		return ctx.begin();
	}

	template <class FormatContext>
	auto format(deai::c_api::di_type type, FormatContext &ctx) {
		return format_to(ctx.out(), "{}", deai::c_api::di_type_names[static_cast<int>(type)]);
	}
};
}        // namespace std

#define DEAI_CPP_PLUGIN_ENTRY_POINT(arg)                                                     \
	/* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                         \
	static auto di_cpp_plugin_init(::deai::Ref<::deai::Core> &&arg) -> int;                  \
	extern "C" visibility_default auto di_plugin_init(::deai::c_api::di_object *di) -> int { \
		return di_cpp_plugin_init(::deai::Ref<::deai::Core>{di});                            \
	}                                                                                        \
	/* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                         \
	static auto di_cpp_plugin_init(::deai::Ref<::deai::Core> &&arg) -> int
