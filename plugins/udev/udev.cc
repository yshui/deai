#include <deai/c++/deai.hh>

#include <libudev.h>

#include "common.h"

using namespace ::deai::c_api;
using namespace ::deai;

namespace {
struct Context {
public:
	static constexpr const char *type [[maybe_unused]] = "deai.plugin.udev:Context";
	udev *context;
	Context() {
		context = udev_new();
	}
	~Context() {
		udev_unref(context);
	}
};

struct Device {
	static constexpr const char *type [[maybe_unused]] = "deai.plugin.udev:Device";
	udev_device *device;

	auto get_properties() -> Ref<Object>;

	Device(Ref<Object> &&ctx_, const char *syspath) {
		auto ctx = std::move(ctx_);        // Move the context object
		auto object_ref = util::unsafe_to_object_ref(*this);
		auto &ctx_inner = util::unsafe_to_inner<Context>(ctx);
		device = udev_device_new_from_syspath(ctx_inner.context, syspath);

		// libudev keeps a ref to `udev` inside udev_device, so we do the same to
		// keep track of the udev context
		object_ref.raw_members()["__udev_context"] = Variant::from(std::move(ctx));

		util::add_method<&Device::get_properties>(*this, "__get_properties");
	}
	~Device() {
		udev_device_unref(device);
	}
};
struct DeviceProperties {
	static constexpr const char *type [[maybe_unused]] = "deai.plugin.udev:"
	                                                     "DeviceProperties";
	auto property_getter(std::string_view name) -> Variant {
		auto object_ref = util::unsafe_to_object_ref(*this);
		auto device_object_ref =
		    *object_ref.raw_members()["__udev_device"]->object_ref();
		auto &device = util::unsafe_to_inner<Device>(device_object_ref);
		const auto *property_value =
		    udev_device_get_property_value(device.device, std::string{name}.c_str());
		if (property_value == nullptr) {
			return Variant::bottom();
		}
		return Variant::from(std::string{property_value});
	}
	DeviceProperties(Ref<Object> &&device) {
		auto object_ref = util::unsafe_to_object_ref(*this);
		object_ref.raw_members()["__udev_device"] = Variant::from(std::move(device));

		util::add_method<&DeviceProperties::property_getter>(*this, "__get");
	}
};

/// udev device properties
///
/// EXPORT: deai.plugin.udev:Device.properties: deai.plugin.udev:DeviceProperties
///
/// A proxy object for udev device properties. Property names are the same ones you can
/// see from running :code:`udevadm info`.
auto Device::get_properties() -> Ref<Object> {
	return util::new_object<DeviceProperties>(util::unsafe_to_object_ref(*this));
}

struct Module {
private:
	auto get_or_create_context() -> Ref<Object> {
		auto object_ref = util::unsafe_to_object_ref(*this);
		auto context_ref = object_ref.raw_members()["__udev_context"];
		std::optional<Variant> maybe_context_ref = context_ref;
		if (maybe_context_ref.has_value()) {
			auto maybe_context =
			    maybe_context_ref->to<WeakRef<Object>>().value().upgrade();
			if (maybe_context.has_value()) {
				return *maybe_context;
			}
		}
		auto context = util::new_object<Context>();
		auto weak_context = context.downgrade();
		context_ref = Variant::from(std::move(weak_context));
		return context;
	}

public:
	static constexpr const char *type [[maybe_unused]] = "deai.plugin.udev:Module";
	/// Create a device object from a device node
	///
	/// EXPORT: udev.device_from_dev_node(path: :string): deai.plugin.udev:Device
	auto device_from_dev_node(std::string_view dev_node) -> Ref<Object> {
		auto context = get_or_create_context();
		auto &context_inner = util::unsafe_to_inner<Context>(context);
		auto *e = udev_enumerate_new(context_inner.context);
		std::string dev_node_copy{dev_node.data(), dev_node.size()};
		udev_enumerate_add_match_property(e, "DEVNAME", dev_node_copy.c_str());
		udev_enumerate_scan_devices(e);

		auto *entry = udev_enumerate_get_list_entry(e);
		auto ret = util::new_object<Device>(std::move(context),
		                                    udev_list_entry_get_name(entry));

		udev_enumerate_unref(e);
		return ret;
	}
};

/// udev
///
/// EXPORT: udev: deai:module
///
/// Interface to the udev Linux subsystem. This is very much work in progress.
auto di_new_udev(::deai::Ref<::deai::Core> &di) {
	auto obj = util::new_object<Module>();
	auto &module = util::unsafe_to_inner<Module>(obj);
	util::add_method<&Module::device_from_dev_node>(module, "device_from_dev_node");
	return obj;
}

DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	auto obj = di_new_udev(di);
	static_cast<void>(di->register_module("udev", obj));
	return 0;
}
}        // namespace
