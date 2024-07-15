#include <deai/c++/deai.hh>

#include <libudev.h>

#include "common.h"
#include "display.hh"

using namespace ::deai::c_api;
using namespace ::deai;

namespace deai::plugins::hwinfo {
struct Module {
public:
	static constexpr const char *type [[maybe_unused]] = "deai.plugin.hwinfo:Module";

	/// Information about display devices, such as monitors.
	///
	/// EXPORT: hwinfo.display: deai.plugin.hwinfo.display:Module
	auto get_display() -> Ref<Object> {
		auto obj = util::new_object<Display>();
		auto &display = util::unsafe_to_inner<Display>(obj);
		util::add_method<&Display::from_edid>(display, "from_edid");
		return obj;
	}
};

/// hwinfo
///
/// EXPORT: hwinfo: deai:module
///
/// General module for handling of harware information.
auto di_new_hwinfo(Ref<Core> &di) -> Ref<Object> {
	auto obj = util::new_object<Module>();
	auto &module = util::unsafe_to_inner<Module>(obj);
	util::add_method<&Module::get_display>(module, "__get_display");
	return obj;
}
DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	auto obj = di_new_hwinfo(di);
	static_cast<void>(di->register_module("hwinfo", obj));
}
}        // namespace deai::plugins::hwinfo
