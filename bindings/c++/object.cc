#include <deai/c++/object.hh>
// #include <iostream>
#include <sstream>

namespace deai {
namespace exception {
auto OtherError::what() const noexcept -> const char * {
	return message.c_str();
}

OtherError::OtherError(int err) : errno_{err} {
	std::stringstream ss;
	ss << "deai error " << errno_;
	message = ss.str();
}

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
using namespace c_api;
void ObjectRefDeleter::operator()(di_object *obj) {
	c_api::di_unref_object(obj);
}
auto Object::operator=(const Object &other) -> Object & {
	inner.reset(c_api::di_ref_object(other.inner.get()));
	return *this;
}
Object::Object(const Object &other) {
	*this = other;
}
Object::Object(c_api::di_object *obj) : inner{obj} {
}
auto Object::unsafe_ref(c_api::di_object *obj) -> Object {
	return Object{obj};
}

auto Object::create() -> Ref<Object> {
	return Ref<Object>{
	    Object{c_api::di_new_object(sizeof(c_api::di_object), alignof(c_api::di_object))}};
}

Variant::~Variant() {
	// std::cerr << "Freeing variant, inner type "
	//           << deai::c_api::di_type_names[static_cast<int>(type)] << " this: " << this
	//           << "\n";
	if (type != c_api::di_type::NIL && type != c_api::di_type::DI_LAST_TYPE) {
		c_api::di_free_value(type, &value);
	}
}

Variant::operator Ref<Object>() {
	return this->object_ref().value();
}

/// Extract an object ref out of this variant. If the variant contains
/// an object ref, it would be moved out and returned. Otherwise nothing happens
/// and nullopt is returned.
auto Variant::object_ref() && -> std::optional<Ref<Object>> {
	if (type == c_api::di_type::OBJECT) {
		// NOLINTNEXTLINE(performance-move-const-arg)
		type = c_api::di_type::NIL;
		return {*Ref<Object>::take(value.object)};
	}
	return std::nullopt;
}

/// Get an object ref out of this variant. The value is copied.
auto Variant::object_ref() & -> std::optional<Ref<Object>> {
	return Variant{*this}.object_ref();
}

auto Variant::unpack() && -> std::vector<Variant> {
	if (type != c_api::di_type::TUPLE) {
		return {std::move(*this)};
	}
	std::vector<Variant> ret{};
	ret.reserve(value.tuple.length);
	for (size_t i = 0; i < value.tuple.length; i++) {
		ret.emplace_back(std::move(value.tuple.elements[i]));
	}
	free(value.tuple.elements);
	type = c_api::di_type::NIL;
	return ret;
}

Variant::Variant(c_api::di_type &&type_, c_api::di_value &&value_)
    : type{type_}, value{value_} {
	// std::cerr << "Creating variant, raw value ctor, inner type "
	//           << deai::c_api::di_type_names[static_cast<int>(type)] << " this: " << this
	//           << "\n";
	type_ = c_api::di_type::NIL;
	::memset(&value_, 0, sizeof(value_));
}
Variant::Variant(c_api::di_variant &&var) : type{var.type} {
	// std::cerr << "Creating variant, variant value ctor, inner type "
	//           << deai::c_api::di_type_names[static_cast<int>(type)] << " this: " << this
	//           << "\n";
	::memcpy(&value, var.value, c_api::di_sizeof_type(type));
	std::free(var.value);
	var.value = nullptr;
	var.type = c_api::di_type::NIL;
}
Variant::Variant(const c_api::di_variant &var) : type{var.type} {
	// std::cerr << "Creating variant, const variant value ctor, inner type "
	//           << deai::c_api::di_type_names[static_cast<int>(type)] << " this: " << this
	//           << "\n";
	c_api::di_copy_value(type, &value, var.value);
}
auto Variant::operator=(const Variant &other) {
	// std::cerr << "Copying variant, inner type "
	//           << deai::c_api::di_type_names[static_cast<int>(other.type)]
	//           << " this: " << this << " other: " << &other << "\n";
	type = other.type;
	c_api::di_copy_value(type, &value, &other.value);
}
Variant::Variant(const Variant &other) {
	// std::cerr << "Creating variant, copy ctor, inner type "
	//           << deai::c_api::di_type_names[static_cast<int>(other.type)]
	//           << " this: " << this << " other: " << &other << "\n";
	*this = other;
}
auto Variant::operator=(Variant &&other) noexcept {
	// std::cerr << "Moving variant, inner type "
	//           << deai::c_api::di_type_names[static_cast<int>(other.type)]
	//           << " this: " << this << " other: " << &other << "\n";
	type = other.type;
	value = other.value;

	other.type = c_api::di_type::NIL;
	other.value = {};
}
Variant::Variant(Variant &&other) noexcept {
	// std::cerr << "Creating variant, move ctor, inner type "
	//           << deai::c_api::di_type_names[static_cast<int>(other.type)]
	//           << " this: " << this << " other: " << &other << "\n";
	*this = std::move(other);
}
auto Variant::nil() -> Variant {
	return {c_api::di_type::NIL, {}};
}

auto Variant::bottom() -> Variant {
	return {c_api::di_type::DI_LAST_TYPE, {}};
}

Variant::operator di_variant() && {
	if (type == c_api::di_type::NIL || type == c_api::di_type::DI_LAST_TYPE) {
		return {nullptr, type};
	}

	c_api::di_variant ret{
	    static_cast<c_api::di_value *>(std::malloc(c_api::di_sizeof_type(type))), type};
	std::memcpy(ret.value, &value, c_api::di_sizeof_type(type));
	type = c_api::di_type::NIL;
	return ret;
}

Variant::operator di_variant() & {
	c_api::di_variant copy = std::move(*this);
	return copy;
}

template <typename T, c_api::di_type Type>
auto Variant::to() && -> std::enable_if_t<std::is_same_v<T, WeakRef<Object>>, std::optional<WeakRef<Object>>> {
	if (type != Type) {
		return std::nullopt;
	}
	type = c_api::di_type::NIL;
	return {WeakRef<Object>{value.weak_object}};
}

Variant::operator std::optional<deai::type::Variant>() && {
	return {std::move(*this)};
}

template auto
Variant::to<WeakRef<Object>, c_api::di_type::WEAK_OBJECT>() && -> std::optional<WeakRef<Object>>;

template <bool raw_>
ObjectMemberProxy<raw_>::operator std::optional<Variant>() const {
	c_api::di_type type;
	c_api::di_value ret;
	if constexpr (raw) {
		if (c_api::di_rawgetx(target, conv::string_to_borrowed_deai_value(key), &type, &ret) != 0) {
			return std::nullopt;
		}
	} else {
		if (c_api::di_getx(target, conv::string_to_borrowed_deai_value(key), &type, &ret) != 0) {
			return std::nullopt;
		}
	}

	// NOLINTNEXTLINE(performance-move-const-arg)
	return std::optional{Variant{std::move(type), std::move(ret)}};
}

template <bool raw_>
void ObjectMemberProxy<raw_>::erase() const {
	if constexpr (raw) {
		c_api::di_delete_member_raw(target, conv::string_to_borrowed_deai_value(key));
	} else {
		c_api::di_delete_member(target, conv::string_to_borrowed_deai_value(key));
	}
}
template <bool raw_>
[[nodiscard]] auto ObjectMemberProxy<raw_>::has_value() const -> bool {
	return static_cast<std::optional<Variant>>(*this).has_value();
}
template <bool raw_>
[[nodiscard]] auto ObjectMemberProxy<raw_>::value() const -> Variant {
	return static_cast<std::optional<Variant>>(*this).value();
}
template <bool raw_>
auto ObjectMemberProxy<raw_>::operator->() const -> std::optional<Variant> {
	return *this;
}

template <bool raw_>
auto ObjectMemberProxy<raw_>::operator*() const -> Variant {
	return *static_cast<std::optional<Variant>>(*this);
}

template <bool raw_>
auto ObjectMemberProxy<raw_>::operator=(const std::optional<Variant> &new_value) const
    -> const ObjectMemberProxy & {
	if constexpr (raw) {
		erase();
		if (new_value.has_value()) {
			int unused rc =
			    c_api::di_add_member_clone(target, conv::string_to_borrowed_deai_value(key),
			                               new_value->type, &new_value->value);
			assert(rc == 0);
		}
	} else {
		if (!new_value.has_value()) {
			erase();
		} else {
			// the setter/deleter should handle the deletion
			exception::throw_deai_error(
			    c_api::di_setx(target, conv::string_to_borrowed_deai_value(key),
			                   new_value->type, &new_value->value));
		}
	}
	return *this;
}
template <bool raw_>
auto ObjectMemberProxy<raw_>::operator=(std::optional<Variant> &&new_value) const
    -> const ObjectMemberProxy & {
	erase();

	auto moved = std::move(new_value);
	if constexpr (raw) {
		if (moved.has_value()) {
			exception::throw_deai_error(c_api::di_add_member_move(
			    target, conv::string_to_borrowed_deai_value(key), &moved->type, &moved->value));
		}
		return *this;
	} else {
		return *this = moved;
	}
}

template struct ObjectMemberProxy<true>;
template struct ObjectMemberProxy<false>;

auto ObjectMembersRawGetter::operator[](const std::string_view &key) -> ObjectMemberProxy<true> {
	return {target, key};
}
ObjectMembersRawGetter::ObjectMembersRawGetter(c_api::di_object *target_)
    : target{target_} {
}
WeakRefBase::WeakRefBase(c_api::di_weak_object *ptr) : inner{ptr} {
}
WeakRefBase::WeakRefBase(const WeakRefBase &other) : inner{nullptr} {
	*this = other;
}
auto WeakRefBase::operator=(const WeakRefBase &other) -> WeakRefBase & {
	c_api::di_weak_object *weak, *weak_other = other.inner.get();
	c_api::di_copy_value(c_api::di_type::WEAK_OBJECT, &weak, &weak_other);
	inner.reset(weak);
	return *this;
}
auto WeakRefBase::release() && -> c_api::di_weak_object * {
	return inner.release();
}
}        // namespace type
}        // namespace deai
