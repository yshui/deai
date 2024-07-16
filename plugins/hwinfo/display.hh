#pragma once

extern "C" {
#include <libdisplay-info/edid.h>
#include <libdisplay-info/info.h>
}

#include <deai/c++/deai.hh>

namespace deai {
namespace plugins::hwinfo {
/// Hardware information module for display devices.
///
/// TYPE: deai.plugin.hwinfo.display:Module
struct Display;
/// Information about a display device.
///
/// TYPE: deai.plugin.hwinfo.display:DisplayInfo
struct DisplayInfo;
}        // namespace plugins::hwinfo

namespace plugins::hwinfo {
struct DisplayInfo {
	static constexpr const char type[] = "deai.plugin.hwinfo.display:DisplayInfo";
	type::ObjectBase base;
	std::unique_ptr<struct di_info, void (*)(struct di_info *)> info;

	DisplayInfo(struct di_info *info) : info{info, di_info_destroy} {
	}
	auto get_model() const -> c_api::String;
	auto get_make() const -> c_api::String;
	auto get_serial() const -> c_api::String;
};
struct Display {
	static constexpr const char type[] = "deai.plugin.hwinfo.display:Module";
	type::ObjectBase base;
	/// Create a display info object by parsing binary EDID data.
	///
	/// EXPORT: hwinfo.display.from_edid(edid: :string): deai.plugin.hwinfo.display:DisplayInfo
	auto from_edid(std::string_view edid) -> Ref<DisplayInfo>;
};

}        // namespace plugins::hwinfo
}        // namespace deai
