#pragma once
#include "deai.hh"

namespace deai::builtin {
namespace log {
struct Log : public Object {
public:
	static constexpr const char *type = "deai.builtin:LogModule";
	[[nodiscard]] auto
	file_target(const std::string &filename, bool overwrite) const -> Ref<Object>;
};
}        // namespace log
namespace event {}
}        // namespace deai::builtin
