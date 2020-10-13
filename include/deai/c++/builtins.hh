#pragma once
#include "deai.hh"

namespace deai::builtin {
namespace log {
struct Log : public Object {
public:
	inline static const std::string type = "deai.builtin:LogModule";
	[[nodiscard]] auto
	file_target(const std::string &filename, bool overwrite) const -> Ref<Object> {
		return util::call_deai_method_raw<Ref<Object>>(inner.get(), "file_target",
		                                               filename, overwrite);
	}
};
}        // namespace log
namespace event {}
}        // namespace deai::builtin
