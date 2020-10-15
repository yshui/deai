#include <deai/c++/deai.hh>

#include <libudev.h>

#include "common.h"

using namespace ::deai::c_api;
using namespace ::deai;

namespace {
struct Context {
public:
	static constexpr const char *type [[maybe_unused]] = "deai.plugin.udev:"
	                                                     "Context";
	udev *context;
	Context() {
		context = udev_new();
	}
	~Context() {
		context = udev_unref(context);
	}
};
struct Device {};

struct Module {
	static constexpr const char *type [[maybe_unused]] = "deai.plugin.udev:"
	                                                     "Module";
	auto device_from_dev_node(std::string_view dev_node) -> Ref<Object> {
		auto object_ref = util::unsafe_to_object_ref(*this);
		auto context_ref = object_ref["__udev_context"];
		auto context = ([&]() {
			if (!context_ref.has_value()) {
				auto context = util::new_object<Context>();
				auto weak_context = context.downgrade();
				context_ref = Variant::from(std::move(weak_context));
				return context;
			}
			return context_ref.value().to<WeakRef<Object>>().value().upgrade().value();
		})();

		auto &context_inner = util::unsafe_to_inner<Context>(context);
		auto e = udev_enumerate_new(context_inner.context);
		std::string dev_node_copy{dev_node.data(), dev_node.size()};
		udev_enumerate_add_match_property(e, "DEVNAME", dev_node_copy.c_str());
		udev_enumerate_scan_devices(e);

		auto entry = udev_enumerate_get_list_entry(e);
		fprintf(stderr, "%s = %s\n", udev_list_entry_get_name(entry), udev_list_entry_get_value(entry));

		udev_enumerate_unref(e);

		return Object::create();
	}
};

DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	auto obj = util::new_object<Module>();
	Module &module = util::unsafe_to_inner<Module>(obj);
	util::add_method<Module, &Module::device_from_dev_node>(module, "device_from_dev_"
	                                                                "node");

	// auto context = util::new_object<Context>();

	static_cast<void>(di->register_module("udev", obj));
	return 0;
}
}        // namespace
