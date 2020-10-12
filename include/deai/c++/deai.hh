#pragma once
#include "object.hh"

namespace deai {

struct CoreRef : type::ObjectRef {

      public:
	inline static const std::string type = "deai:Core";
};
}        // namespace deai
