#include <deai/c++/deai.hh>
#include <cassert>

DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	// Move
	auto log = std::move(di["log"]).object_ref();
	assert(log.has_value());

	auto log2 = di["log"];
	// Copy
	auto log3 = log2.object_ref();
	return 0;
}

