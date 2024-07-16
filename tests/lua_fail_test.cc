#include <cassert>
#include <deai/c++/builtins.hh>
#include <deai/c++/deai.hh>
#include <iostream>
#include <string_view>

#include <unistd.h>

using namespace deai::type;
using namespace deai::util;
using namespace std::literals;

static constexpr const char *expected_error_log =
    "Error while running lua script: Failed to run lua script ../tests/invalid.lua: "
    "../tests/invalid.lua:1: attempt to call global 'non_existent' (a nil value)\nstack "
    "traceback:";

DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	auto luam = di["lua"]->object_ref().value();

	try {
		luam.method_call<void>("load_script", "../tests/invalid.lua"sv);
	} catch (deai::c_api::Object *&e) {
		auto err = *Ref<Object>::take(e);
		auto errmsg = err["errmsg"]->to<std::string>().value();
		std::cout << "Caught error: " << err["errmsg"]->to<std::string>().value() << '\n';
		assert(errmsg == expected_error_log);
	}
}
