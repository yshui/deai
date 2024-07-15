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
void ObjectRefDeleter::operator()(di_object *obj) {
	::di_unref_object(obj);
}
auto Object::operator=(const Object &other) -> Object & {
	inner.reset(::di_ref_object(other.inner.get()));
	return *this;
}
Object::Object(const Object &other) {
	*this = other;
}
Object::Object(c_api::Object *obj) : inner{obj} {
}
auto Object::unsafe_ref(c_api::Object *obj) -> Object {
	return Object{obj};
}

auto Object::create() -> Ref<Object> {
	return Ref<Object>{Object{::di_new_object(sizeof(c_api::Object), alignof(c_api::Object))}};
}

Variant::~Variant() {
	// std::cerr << "Freeing variant, inner type "
	//           << deai::c_api::Type_names[static_cast<int>(type)] << " this: " << this
	//           << "\n";
	if (type != c_api::Type::NIL && type != c_api::Type::DI_LAST_TYPE) {
		::di_free_value(type, &value);
	}
}

Variant::operator Ref<Object>() {
	return this->object_ref().value();
}

/// Extract an object ref out of this variant. If the variant contains
/// an object ref, it would be moved out and returned. Otherwise nothing happens
/// and nullopt is returned.
auto Variant::object_ref() && -> std::optional<Ref<Object>> {
	if (type == c_api::Type::OBJECT) {
		// NOLINTNEXTLINE(performance-move-const-arg)
		type = c_api::Type::NIL;
		return {*Ref<Object>::take(value.object)};
	}
	return std::nullopt;
}

/// Get an object ref out of this variant. The value is copied.
auto Variant::object_ref() & -> std::optional<Ref<Object>> {
	return Variant{*this}.object_ref();
}

Variant::operator std::optional<Ref<Object>>() && {
	return std::move(*this).object_ref();
}

auto Variant::unpack() && -> std::vector<Variant> {
	if (type != c_api::Type::TUPLE) {
		return {std::move(*this)};
	}
	std::vector<Variant> ret{};
	ret.reserve(value.tuple.length);
	for (size_t i = 0; i < value.tuple.length; i++) {
		ret.emplace_back(std::move(value.tuple.elements[i]));
	}
	free(value.tuple.elements);
	type = c_api::Type::NIL;
	return ret;
}

Variant::Variant(c_api::Type &&type_, ::di_value &&value_) : type{type_}, value{value_} {
	// std::cerr << "Creating variant, raw value ctor, inner type "
	//           << deai::c_api::Type_names[static_cast<int>(type)] << " this: " << this
	//           << "\n";
	type_ = c_api::Type::NIL;
	::memset(&value_, 0, sizeof(value_));
}
Variant::Variant(::di_variant &&var) : type{var.type} {
	// std::cerr << "Creating variant, variant value ctor, inner type "
	//           << deai::c_api::Type_names[static_cast<int>(type)] << " this: " << this
	//           << "\n";
	::memcpy(&value, var.value, ::di_sizeof_type(type));
	std::free(var.value);
	var.value = nullptr;
	var.type = c_api::Type::NIL;
}
Variant::Variant(const ::di_variant &var) : type{var.type} {
	// std::cerr << "Creating variant, const variant value ctor, inner type "
	//           << deai::c_api::Type_names[static_cast<int>(type)] << " this: " << this
	//           << "\n";
	::di_copy_value(type, &value, var.value);
}
auto Variant::operator=(const Variant &other) {
	// std::cerr << "Copying variant, inner type "
	//           << deai::c_api::Type_names[static_cast<int>(other.type)]
	//           << " this: " << this << " other: " << &other << "\n";
	type = other.type;
	::di_copy_value(type, &value, &other.value);
}
Variant::Variant(const Variant &other) {
	// std::cerr << "Creating variant, copy ctor, inner type "
	//           << deai::c_api::Type_names[static_cast<int>(other.type)]
	//           << " this: " << this << " other: " << &other << "\n";
	*this = other;
}
auto Variant::operator=(Variant &&other) noexcept {
	// std::cerr << "Moving variant, inner type "
	//           << deai::c_api::Type_names[static_cast<int>(other.type)]
	//           << " this: " << this << " other: " << &other << "\n";
	type = other.type;
	value = other.value;

	other.type = c_api::Type::NIL;
	other.value = {};
}
Variant::Variant(Variant &&other) noexcept {
	// std::cerr << "Creating variant, move ctor, inner type "
	//           << deai::c_api::Type_names[static_cast<int>(other.type)]
	//           << " this: " << this << " other: " << &other << "\n";
	*this = std::move(other);
}
auto Variant::nil() -> Variant {
	return {c_api::Type::NIL, {}};
}

auto Variant::bottom() -> Variant {
	return {c_api::Type::DI_LAST_TYPE, {}};
}

Variant::operator di_variant() && {
	if (type == c_api::Type::NIL || type == c_api::Type::DI_LAST_TYPE) {
		return {nullptr, type};
	}

	::di_variant ret{static_cast<::di_value *>(std::malloc(::di_sizeof_type(type))), type};
	std::memcpy(ret.value, &value, ::di_sizeof_type(type));
	type = c_api::Type::NIL;
	return ret;
}

Variant::operator di_variant() & {
	::di_variant copy = std::move(*this);
	return copy;
}

Variant::operator std::optional<WeakRef<Object>>() && {
	if (type != c_api::Type::WEAK_OBJECT) {
		return std::nullopt;
	}
	type = c_api::Type::NIL;
	return {WeakRef<Object>{value.weak_object}};
}

Variant::operator std::optional<deai::type::Variant>() && {
	return {std::move(*this)};
}

template <bool raw_>
ObjectMemberProxy<raw_>::operator std::optional<Variant>() const {
	c_api::Type type;
	::di_value ret;
	if constexpr (raw) {
		if (::di_rawgetx(target, conv::string_to_borrowed_deai_value(key), &type, &ret) != 0) {
			return std::nullopt;
		}
	} else {
		if (::di_getx(target, conv::string_to_borrowed_deai_value(key), &type, &ret) != 0) {
			return std::nullopt;
		}
	}

	// NOLINTNEXTLINE(performance-move-const-arg)
	return std::optional{Variant{std::move(type), std::move(ret)}};
}

template <bool raw_>
void ObjectMemberProxy<raw_>::erase() const {
	if constexpr (raw) {
		::di_delete_member_raw(target, conv::string_to_borrowed_deai_value(key));
	} else {
		::di_delete_member(target, conv::string_to_borrowed_deai_value(key));
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
			    ::di_add_member_clone(target, conv::string_to_borrowed_deai_value(key),
			                          new_value->type, &new_value->value);
			assert(rc == 0);
		}
	} else {
		if (!new_value.has_value()) {
			erase();
		} else {
			// the setter/deleter should handle the deletion
			exception::throw_deai_error(::di_setx(target, conv::string_to_borrowed_deai_value(key),
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
			exception::throw_deai_error(::di_add_member_move(
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
ObjectMembersRawGetter::ObjectMembersRawGetter(c_api::Object *target_) : target{target_} {
}
WeakRefBase::WeakRefBase(c_api::WeakObject *ptr) : inner{ptr} {
}
WeakRefBase::WeakRefBase(const WeakRefBase &other) : inner{nullptr} {
	*this = other;
}
auto WeakRefBase::operator=(const WeakRefBase &other) -> WeakRefBase & {
	c_api::WeakObject *weak, *weak_other = other.inner.get();
	::di_copy_value(c_api::Type::WEAK_OBJECT, &weak, &weak_other);
	inner.reset(weak);
	return *this;
}
auto WeakRefBase::release() && -> c_api::WeakObject * {
	return inner.release();
}
}        // namespace type
}        // namespace deai
