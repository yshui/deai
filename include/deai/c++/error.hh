#include <source_location>
#include <string_view>

#include "c_api.hh"
namespace deai::util {
auto new_error(std::string_view message,
               std::source_location location = std::source_location::current()) -> c_api::Object *;
}        // namespace deai::util
