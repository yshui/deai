#include "display.hh"

namespace deai::plugins::hwinfo {
auto Display::from_edid(std::string_view edid) -> Ref<::deai::Object> {
	auto obj = util::new_object<DisplayInfo>(::di_info_parse_edid(edid.data(), edid.size()));
	DisplayInfo::init_object(obj);
	return obj;
}
auto DisplayInfo::get_model() const -> c_api::String {
	auto *name = ::di_info_get_model(info.get());
	return c_api::string::borrow(name);
}
auto DisplayInfo::get_make() const -> c_api::String {
	auto *name = ::di_info_get_make(info.get());
	return c_api::string::borrow(name);
}
auto DisplayInfo::get_serial() const -> c_api::String {
	auto *name = ::di_info_get_serial(info.get());
	return c_api::string::borrow(name);
}
void DisplayInfo::init_object(Ref<::deai::Object> &obj) {
	auto &self = util::unsafe_to_inner<DisplayInfo>(obj);
	util::add_method<&DisplayInfo::get_model>(self, "__get_model");
	util::add_method<&DisplayInfo::get_make>(self, "__get_make");
	util::add_method<&DisplayInfo::get_serial>(self, "__get_serial");
}
}        // namespace deai::plugins::hwinfo
