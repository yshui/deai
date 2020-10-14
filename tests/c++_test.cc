#include <deai/c++/deai.hh>
#include <deai/c++/builtins.hh>
#include <cassert>

#include <unistd.h>
#include <climits>

using namespace deai::type;
DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	auto log = *di["log"];

	// Copy
	auto log2 = log.object_ref();
	assert(log.is<Ref<Object>>());

	// Move
	auto log3 = std::move(log).object_ref();
	assert(log3.has_value());

	assert(log.is<void>()); // NOLINT(bugprone-use-after-move)

	auto log_module = std::move(*log3).downcast<deai::builtin::log::Log>().value();
	auto file_target = log_module->file_target("/tmp/file", false);

	di->chdir("/tmp");

	char path[PATH_MAX]; // NOLINT
	assert(strcmp(getcwd(path, sizeof(path)), "/tmp") == 0);
	return 0;
}

