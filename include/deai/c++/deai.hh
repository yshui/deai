#pragma once
#include "object.hh"

namespace deai {

struct Core {
	static constexpr const char type[] = "deai:Core";
	type::ObjectBase base;
	void load_plugin(std::string_view plugin_name);
	void chdir(std::string_view new_dir);
	void exit(int exit_code);
	void quit();
	void terminate();
	[[nodiscard]] auto load_plugin_from_dir(std::string_view plugin) -> int;
	[[nodiscard]] auto
	register_module(std::string_view module_name, const Ref<Object> &module) -> int;
};
}        // namespace deai
