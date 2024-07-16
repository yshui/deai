#include <deai/c++/deai.hh>

namespace deai {
void Core::load_plugin(std::string_view plugin_name) {
	util::call_raw<void>(&base, "load_plugin", plugin_name);
}
void Core::chdir(std::string_view new_dir) {
	util::call_raw<void>(&base, "chdir", new_dir);
}
void Core::exit(int exit_code) {
	util::call_raw<void>(&base, "exit", exit_code);
}
auto Core::load_plugin_from_dir(std::string_view plugin_dir) -> int {
	return util::call_raw<int>(&base, "load_plugin_from_dir", plugin_dir);
}
auto Core::register_module(std::string_view module_name, const Ref<Object> &module) -> int {
	return util::call_raw<int>(&base, "register_module", module_name, module);
}
}        // namespace deai
