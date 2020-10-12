#pragma once
#include "deai.hh"

namespace deai::builtin {
namespace log {
struct LogRef : public ObjectRef {
public:
	inline static const std::string type = "deai.builtin:LogModule";
	[[nodiscard]] auto
	file_target(const std::string &filename, bool overwrite) const -> ObjectRef {
		return util::call_deai_method<ObjectRef>(*this, "file_target", filename, overwrite);
	}
};
}        // namespace log
namespace event {}
}        // namespace deai::builtin
