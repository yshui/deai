#include <deai/c++/builtins.hh>
namespace deai::builtin {
namespace log {

[[nodiscard]] auto
Log::file_target(const std::string &filename, bool overwrite) const -> Ref<Object> {
	return util::call_deai_method_raw<Ref<Object>>(inner.get(), "file_target",
	                                               filename, overwrite);
}
}        // namespace log
namespace event {}
}        // namespace deai::builtin
