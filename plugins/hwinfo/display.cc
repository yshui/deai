#include "display.hh"

namespace deai::plugins::hwinfo {
auto Display::from_edid(std::string_view edid) -> Ref<DisplayInfo> {
	auto obj = util::new_object<DisplayInfo>(::di_info_parse_edid(edid.data(), edid.size()));
	util::add_method<&DisplayInfo::get_model>(obj, "__get_model");
	util::add_method<&DisplayInfo::get_make>(obj, "__get_make");
	util::add_method<&DisplayInfo::get_serial>(obj, "__get_serial");
	return obj;
}
auto DisplayInfo::get_model() const -> c_api::String {
	auto *name = ::di_info_get_model(info.get());
	if (name == nullptr) {
		return DI_STRING_INIT;
	}
	return c_api::string::borrow(name);
}
auto DisplayInfo::get_make() const -> c_api::String {
	auto *name = ::di_info_get_make(info.get());
	if (name == nullptr) {
		return DI_STRING_INIT;
	}
	return c_api::string::borrow(name);
}
auto DisplayInfo::get_serial() const -> c_api::String {
	auto *name = ::di_info_get_serial(info.get());
	if (name == nullptr) {
		return DI_STRING_INIT;
	}
	return c_api::string::borrow(name);
}
}        // namespace deai::plugins::hwinfo
