#pragma once
#include "object.hh"

namespace deai {

struct Core : type::Object {
	inline static const std::string type = "deai:Core";
	void load_plugin(const std::string &plugin_name) const {
		util::call_deai_method_raw<void>(this->inner.get(), "load_plugin", plugin_name);
	}
	void chdir(const std::string &new_dir) const {
		util::call_deai_method_raw<void>(this->inner.get(), "chdir", new_dir);
	}
};
}        // namespace deai