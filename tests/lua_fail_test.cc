#include <cassert>
#include <deai/c++/builtins.hh>
#include <deai/c++/deai.hh>
#include <iostream>
#include <string_view>

#include <unistd.h>

using namespace deai::type;
using namespace deai::util;
using namespace std::literals;

struct StringLogTarget {
	static constexpr const char *type = "deai.tests:StringLogTarget";
	ObjectBase base;
	std::string log;
	auto write(std::string_view message) -> int {
		int written = (int)message.size();
		log += message;
		if (!message.ends_with('\n')) {
			log += '\n';
			written += 1;
		}
		return written;
	}
};

static constexpr const char *expected_error_log =
    "Error while running lua script: Failed to run lua script ../tests/invalid.lua: "
    "../tests/invalid.lua:1: attempt to call global 'non_existent' (a nil value)\nstack "
    "traceback:\n";

DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	auto log_target = util::new_object<StringLogTarget>();
	util::add_method<&StringLogTarget::write>(log_target, "write");

	di["log"]->object_ref().value()["log_target"] = Variant::from(log_target.clone());

	auto luam = di["lua"]->object_ref().value();

	luam.method_call<void>("load_script", "../tests/invalid.lua"sv);
	std::cout << log_target->log << '\n';
	assert(log_target->log == expected_error_log);
}
