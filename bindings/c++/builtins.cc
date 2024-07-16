#include <deai/c++/builtins.hh>
namespace deai::builtin {
namespace log {

[[nodiscard]] auto Log::file_target(const std::string &filename, bool overwrite) -> Ref<Object> {
	return util::call_raw<Ref<Object>>(&base, "file_target", filename, overwrite);
}
}        // namespace log
namespace event {}
}        // namespace deai::builtin
