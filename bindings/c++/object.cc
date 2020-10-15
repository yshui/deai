#include <deai/c++/object.hh>

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
	return Ref<Object>{Object{
	    c_api::di_new_object(sizeof(c_api::di_object), alignof(c_api::di_object))}};
}

Variant::~Variant() {
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

Variant::Variant(c_api::di_type type_, const c_api::di_value &value_)
    : type{type_}, value{value_} {
	type_ = c_api::di_type::NIL;
}
Variant::Variant(const c_api::di_variant &var) : type{var.type} {
	memcpy(&value, var.value, c_api::di_sizeof_type(type));
	std::free(var.value);
}
auto Variant::operator=(const Variant &other) {
	type = other.type;
	c_api::di_copy_value(type, &value, &other.value);
}
Variant::Variant(const Variant &other) {
	*this = other;
}
auto Variant::operator=(Variant &&other) noexcept {
	type = other.type;
	value = other.value;

	other.type = c_api::di_type::NIL;
	other.value = {};
}
Variant::Variant(Variant &&other) noexcept {
	*this = std::move(other);
}
auto Variant::nil() -> Variant {
	return {c_api::di_type::NIL, {}};
}

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
