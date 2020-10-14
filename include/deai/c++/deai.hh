#pragma once
#include "object.hh"

namespace deai {

struct Core : type::Object {
	static constexpr const char *type = "deai:Core";
	void load_plugin(std::string_view plugin_name) const;
	void chdir(std::string_view new_dir) const;
	void exit(int exit_code) const;
	void quit() const;
	void terminate() const;
	[[nodiscard]] auto load_plugin_from_dir(std::string_view plugin) const -> int;
	[[nodiscard]] auto
	register_module(std::string_view module_name, const Ref<Object> &module) const -> int;
};
}        // namespace deai
