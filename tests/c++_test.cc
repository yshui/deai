#include <cassert>
#include <deai/c++/builtins.hh>
#include <deai/c++/deai.hh>
#include <string>

#include <unistd.h>
#include <climits>

using namespace deai::type;
using namespace std::literals;
thread_local static int result = 0;
auto test_function(int a) -> int {
	result = a;
	return a;
}
DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	auto log = *di["log"];

	// Copy
	auto log2 = log.object_ref();
	assert(log.is<Ref<Object>>());

	// Move
	auto log3 = std::move(log).object_ref();
	assert(log3.has_value());

	assert(log.is<void>());        // NOLINT(bugprone-use-after-move)

	auto log_module = std::move(*log3).downcast<deai::builtin::log::Log>().value();
	auto file_target = log_module->file_target("/tmp/file", false);

	di->chdir("/tmp");

	std::array<char, PATH_MAX> path;
	assert(strcmp(getcwd(path.data(), path.size()), "/tmp") == 0);
	auto object = util::new_object<Object>();

	// Test move assignment
	object["test_member"] = std::optional{Variant::from("test_member_value"s)};
	assert(object["test_member"]->to<std::string>() == "test_member_value"s);

	Ref<ListenHandle> lh = ([&]() {
		// Drop closure after `.on`, to make sure it's indeed kept alive
		auto closure = util::to_di_callable<test_function>();
		assert(closure.call<int>(10) == 10);
		// Test integer conversions
		assert(closure.call<int>(static_cast<int64_t>(10)) == 10);
		assert(closure.call<int64_t>(static_cast<int>(10)) == 10);

		return object.on("test_signal", closure);
	})();
	object.emit("test_signal", 20);        // NOLINT
	assert(result == 20);
}
