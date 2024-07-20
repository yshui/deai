#include <cassert>
#include <deai/c++/builtins.hh>
#include <deai/c++/deai.hh>
#include <iostream>
#include <string_view>

#include <unistd.h>

using namespace deai::type;
using namespace deai;
using namespace std::literals;

static constexpr const char *expected_error_log =
    "../tests/invalid.lua:1: attempt to call global 'non_existent' (a nil value)";

static constexpr const char *chained_error = "This is a chained error";
static constexpr const char test_error[] = "This is a test error";
struct Thrower {
	ObjectBase base;
	static constexpr const char type[] = "deai.tests:Thrower";
	void throw_error() {
		auto err1 = util::new_error(chained_error);
		auto err2 = util::new_error(test_error);

		deai::c_api::String prop = {
		    .data = "source",
		    .length = 6,
		};
		auto type = c_api::Type::OBJECT;
		deai::c_api::object::add_member_move(err2, prop, &type, &err1);
		throw err2;
	}
};

DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	auto luam = di["lua"]->object_ref().value();

	auto vs = luam.method_call<Variant>("load_script", "../tests/script_ret.lua"sv).unpack();
	auto obj = vs[0].object_ref().value();

	assert(obj["a"]->to<int64_t>() == 1);
	assert(obj["b"]->to<std::string>() == "asdf");
	std::cout << obj.to_string() << '\n';
	assert(obj.to_string() == "this is a tostring test");

	bool caught = false;
	try {
		luam.method_call<void>("load_script", "../tests/invalid.lua"sv);
	} catch (deai::c_api::Object *&e) {
		auto err = *Ref<Object>::take(e);
		auto error = err["error"]->to<std::string>().value();
		std::cout << "Caught error: " << err["error"]->to<std::string>().value() << '\n';
		assert(error == expected_error_log);
		caught = true;
	}
	assert(caught);

	auto thrower = util::new_object<Thrower>();
	util::add_method<&Thrower::throw_error>(thrower, "throw");
	di["thrower"] = Variant::from(std::move(thrower));

	caught = false;
	try {
		luam.method_call<void>("load_script", "../tests/c++_throw.lua"sv);
	} catch (deai::c_api::Object *&e) {
		auto err = *Ref<Object>::take(e);
		auto error = err["error"]->to<std::string>().value();
		std::cout << "Caught error: " << err["error"]->to<std::string>().value() << '\n';
		std::cout << err.to_string() << "\n";
		assert(error == test_error);
		caught = true;
	}
	assert(caught);
}
