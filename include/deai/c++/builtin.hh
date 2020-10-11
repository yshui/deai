#include <deai/c++/deai.hh>

namespace deai::builtin {
namespace log {
struct LogRef : public ObjectRef {
      public:
	inline static const std::string type = "deai.builtin:LogModule";
	[[nodiscard]] auto
	file_target(const std::string &filename, bool overwrite) const -> ObjectRef {
		c_api::di_type_t type;
		c_api::di_value value;
		bool called;

		std::array<c_api::di_variant, 2> args;
		const char *c_filename = filename.c_str();
		args[0].type = c_api::DI_TYPE_STRING_LITERAL;
		args[0].value = reinterpret_cast<c_api::di_value *>(&c_filename);
		args[1].type = c_api::DI_TYPE_BOOL;
		args[1].value = reinterpret_cast<c_api::di_value *>(&overwrite);

		int rc = c_api::di_rawcallxn(inner.get(), "file_target", &type, &value,
		                             c_api::di_tuple{2, args.data()}, &called);
		assert(rc == 0);
		assert(called);
		assert(type == c_api::DI_TYPE_OBJECT);
		return ObjectRef{value.object};
	}
};
}        // namespace log
namespace event {}
}        // namespace deai
