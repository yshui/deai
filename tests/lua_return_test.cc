#include <cassert>
#include <deai/c++/builtins.hh>
#include <deai/c++/deai.hh>
#include <iostream>
#include <string_view>

#include <unistd.h>

using namespace deai::type;
using namespace deai::util;
using namespace std::literals;

DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	auto luam = di["lua"]->object_ref().value();

	auto vs = luam.method_call<Variant>("load_script", "../tests/script_ret.lua"sv).unpack();
	auto obj = vs[0].object_ref().value();

	assert(obj["a"]->to<int64_t>() == 1);
	assert(obj["b"]->to<std::string>() == "asdf");
}
