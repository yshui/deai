#include <deai/c++/deai.hh>

namespace deai {
void Core::load_plugin(std::string_view plugin_name) const {
	util::call_deai_method_raw<void>(inner.get(), "load_plugin", plugin_name);
}
void Core::chdir(std::string_view new_dir) const {
	util::call_deai_method_raw<void>(inner.get(), "chdir", new_dir);
}
void Core::exit(int exit_code) const {
	util::call_deai_method_raw<void>(inner.get(), "exit", exit_code);
}
auto Core::load_plugin_from_dir(std::string_view plugin_dir) const -> int {
	return util::call_deai_method_raw<int>(inner.get(), "load_plugin_from_dir", plugin_dir);
}
auto Core::register_module(std::string_view module_name, const Ref<Object> &module) const -> int {
	return util::call_deai_method_raw<int>(inner.get(), "register_module", module_name, module);
}
}        // namespace deai
