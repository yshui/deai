#pragma once
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
namespace deai {
namespace c_api {
extern "C" {
#define __auto_type auto
#include <deai/deai.h>
#undef __auto_type
}
}        // namespace c_api

struct Variant;

struct ObjectRefDeleter {
	void operator()(c_api::di_object *obj) {
		c_api::di_unref_object(obj);
	}
};

struct ObjectRef {
      private:
	std::unique_ptr<c_api::di_object, ObjectRefDeleter> inner;

      public:
	/// Create an owning Object reference from an owning c_api
	/// object reference.
	ObjectRef(c_api::di_object *&obj) : inner(obj) {
		obj = nullptr;
	}

	/// ditto
	ObjectRef(c_api::di_object *&&obj) : ObjectRef(obj) {
	}

	/// Clone this reference. This will increment the reference count on the
	/// inner di_object.
	[[nodiscard]] auto clone() const -> ObjectRef {
		return ObjectRef{c_api::di_ref_object(inner.get())};
	}

	auto operator[](const std::string &key) -> Variant;
};
struct Variant {
      private:
	c_api::di_type type;
	c_api::di_value value;

      public:
	~Variant() {
		c_api::di_free_value(type, &value);
	}

	/// Takes ownership of `value_`
	Variant(c_api::di_type &type_, c_api::di_value &value_)
	    : type{type_}, value{value_} {
		type_ = c_api::DI_TYPE_NIL;
		value_ = {};
	}

	// ditto
	Variant(c_api::di_type &&type_, c_api::di_value &&value_)
	    : Variant{type_, value_} {
	}

	/// Takes ownership of `var`
	Variant(c_api::di_variant &var) : type{var.type} {
		memcpy(&value, var.value, c_api::di_sizeof_type(type));
		var.type = c_api::DI_TYPE_NIL;
		std::free(var.value);
		var.value = nullptr;
	}

	Variant(const Variant &other) {
		type = other.type;
		c_api::di_copy_value(other.type, &value, &other.value);
	}

	Variant(Variant &&other) noexcept {
		type = other.type;
		value = other.value;

		other.type = c_api::DI_TYPE_NIL;
		other.value = {};
	}

	static auto nil() -> Variant {
		return {c_api::DI_TYPE_NIL, {}};
	}

	/// Extract an object ref out of this variant. If the variant contains
	/// an object ref, it would be moved out and returned. Otherwise nothing happens
	/// and nullopt is returned.
	auto object_ref() && -> std::optional<ObjectRef> {
		if (type == c_api::DI_TYPE_OBJECT) {
			auto ret = ObjectRef(value.object);
			type = c_api::DI_TYPE_NIL;
			value = {};
			return ret;
		}
		return std::nullopt;
	}

	/// Get an object ref out of this variant. The value is copied.
	auto object_ref() & -> std::optional<ObjectRef> {
		return Variant{*this}.object_ref();
	}
};

inline auto ObjectRef::operator[](const std::string &key) -> Variant {
	c_api::di_type_t type;
	c_api::di_value ret;
	if (c_api::di_getx(inner.get(), key.c_str(), &type, &ret) != 0) {
		return Variant::nil();
	}

	return Variant{type, ret};
}

}        // namespace deai

#define DEAI_CPP_PLUGIN_ENTRY_POINT(arg)                                                     \
	static auto di_cpp_plugin_init(deai::ObjectRef &&arg)->int;                          \
	extern "C" visibility_default auto di_plugin_init(deai::c_api::di_object *di)->int { \
		return di_cpp_plugin_init(deai::ObjectRef{deai::c_api::di_ref_object(di)});  \
	}                                                                                    \
	static auto di_cpp_plugin_init(deai::ObjectRef &&arg)->int
