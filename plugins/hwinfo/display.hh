#pragma once

extern "C" {
#include <libdisplay-info/edid.h>
#include <libdisplay-info/info.h>
}

#include <deai/c++/deai.hh>

namespace deai::plugins::hwinfo {
/// Hardware information module for display devices.
///
/// TYPE: deai.plugin.hwinfo.display:Module
class Display {
public:
	static constexpr const char *type [[maybe_unused]] =
	    "deai.plugin.hwinfo.display:Module";

	/// Create a display info object by parsing binary EDID data.
	///
	/// EXPORT: hwinfo.display.from_edid(edid: :string): deai.plugin.hwinfo.display:DisplayInfo
	auto from_edid(std::string_view edid) -> Ref<::deai::Object>;
};

/// Information about a display device.
///
/// TYPE: deai.plugin.hwinfo.display:DisplayInfo
class DisplayInfo {
private:
	std::unique_ptr<struct di_info, void (*)(struct di_info *)> info;

public:
	static constexpr const char *type [[maybe_unused]] =
	    "deai.plugin.hwinfo.display:DisplayInfo";

	DisplayInfo(struct di_info *info) : info{info, di_info_destroy} {
	}
	static void init_object(Ref<::deai::Object> &obj);
	auto get_model() const -> c_api::di_string;
	auto get_make() const -> c_api::di_string;
	auto get_serial() const -> c_api::di_string;
};
}        // namespace deai::plugins::hwinfo
