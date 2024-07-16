#pragma once
#include "deai.hh"

namespace deai {
namespace builtin {
namespace log {
struct Log {
	static constexpr const char type[] = "deai.builtin:LogModule";
	type::ObjectBase base;
	[[nodiscard]] auto file_target(const std::string &filename, bool overwrite) -> Ref<Object>;
};
}        // namespace log
namespace event {}
}        // namespace builtin

}        // namespace deai
