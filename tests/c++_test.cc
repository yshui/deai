#include <deai/c++/deai.hh>
#include <deai/c++/builtin.hh>
#include <cassert>

DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	// Move
	auto log = std::move(di["log"]).object_ref();
	assert(log.has_value());

	auto log2 = di["log"];
	// Copy
	auto log3 = log2.object_ref();

	auto log_module = std::move(*log3).cast<deai::builtin::log::LogRef>();
	auto file_target = log_module->file_target("/tmp/file", false);
	return 0;
}

