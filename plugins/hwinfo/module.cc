#include <deai/c++/deai.hh>

#include <libudev.h>

#include "common.h"
#include "display.hh"

using namespace ::deai;

namespace {
using namespace ::deai::plugins::hwinfo;
struct Module {
	static constexpr const char type[] = "deai.plugin.hwinfo:Module";
	type::ObjectBase base;

	/// Information about display devices, such as monitors.
	///
	/// EXPORT: hwinfo.display: deai.plugin.hwinfo.display:Module
	auto get_display() -> Ref<Display> {
		auto display = util::new_object<Display>();
		util::add_method<&Display::from_edid>(display, "from_edid");
		return display;
	}
};
}        // namespace

namespace {
/// hwinfo
///
/// EXPORT: hwinfo: deai:module
///
/// General module for handling of harware information.
auto di_new_hwinfo(Ref<Core> &di) -> Ref<Module> {
	auto module = util::new_object<Module>();
	util::add_method<&Module::get_display>(module, "__get_display");
	return module;
}
DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	auto obj = di_new_hwinfo(di);
	static_cast<void>(di->register_module("hwinfo", std::move(obj).cast()));
}
}        // namespace
