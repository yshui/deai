#include "display.hh"

namespace deai::plugins::hwinfo {
auto Display::from_edid(std::string_view edid) -> Ref<::deai::Object> {
	auto obj =
	    util::new_object<DisplayInfo>(::di_info_parse_edid(edid.data(), edid.size()));
	DisplayInfo::init_object(obj);
	return obj;
}
auto DisplayInfo::get_model() const -> std::string {
	auto *name = ::di_info_get_model(info.get());
	return std::string{name};
}
void DisplayInfo::init_object(Ref<::deai::Object> &obj) {
	auto &self = util::unsafe_to_inner<DisplayInfo>(obj);
	util::add_method<&DisplayInfo::get_model>(self, "__get_model");
}
}        // namespace deai::plugins::hwinfo
