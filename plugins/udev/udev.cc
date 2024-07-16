#include <deai/c++/deai.hh>

#include <libudev.h>

using namespace ::deai;
namespace {
struct Context;
struct Device;
struct DeviceProperties;
struct Enumerator;
struct Module;
struct Context;
}        // namespace
namespace {
struct Context {
	static constexpr const char type[] = "deai.plugin.udev:Context";
	ObjectBase base;
	udev *context;
	Context() {
		context = udev_new();
	}
	~Context() {
		udev_unref(context);
	}
};

struct DeviceProperties {
	static constexpr const char type[] = "deai.plugin.udev:DeviceProperties";
	ObjectBase base;
	auto property_getter(std::string_view name) -> Variant;

	template <typeinfo::DerivedObject T>
	DeviceProperties(Ref<T> &&device);        // We only implement this for Ref<Device>,
	                                          // can't write that directly here because
	                                          // `Device` is incomplete at this point.
};

struct Device {
	static constexpr const char type[] = "deai.plugin.udev:Device";
	ObjectBase base;
	udev_device *device;

	/// udev device properties
	///
	/// EXPORT: deai.plugin.udev:Device.properties: deai.plugin.udev:DeviceProperties
	///
	/// A proxy object for udev device properties. Property names are the same ones you
	/// can see from running :code:`udevadm info`.
	auto get_properties() -> Ref<DeviceProperties>;

	Device(Ref<Context> &&ctx_, const char *syspath);
	~Device() {
		udev_device_unref(device);
	}
};

/// TYPE: deai.plugin.udev:Enumerator
struct Enumerator {
	static constexpr const char type[] = "deai.plugin.udev:Enumerator";
	ObjectBase base;
	udev_enumerate *raw;
	Enumerator(udev *udev) : raw(udev_enumerate_new(udev)) {
	}
};

struct Module {
	static constexpr const char type[] = "deai.plugin.udev:Module";
	ObjectBase base;
	auto get_or_create_context() -> Ref<Context>;
	/// Create a device object from a device node
	///
	/// EXPORT: udev.device_from_dev_node(path: :string): deai.plugin.udev:Device
	auto device_from_dev_node(std::string_view dev_node) -> Ref<Device>;
	/// Enumerate udev devices matching certain criteria
	///
	/// EXPORT: udev.search(): deai.plugin.udev:Enumerator
	auto search() -> Ref<Enumerator>;
};
}        // namespace

namespace {

auto DeviceProperties::property_getter(std::string_view name) -> Variant {
	auto object_ref = Ref<DeviceProperties>{*this};
	auto device =
	    *(*object_ref.raw_members()["__udev_device"]->object_ref()).downcast<Device>();
	const auto *property_value =
	    udev_device_get_property_value(device->device, std::string{name}.c_str());
	if (property_value == nullptr) {
		return Variant::bottom();
	}
	return Variant::from(std::string{property_value});
}
template <>
DeviceProperties::DeviceProperties(Ref<Device> &&device) {
	auto object_ref = Ref<DeviceProperties>{*this};
	object_ref.raw_members()["__udev_device"] = Variant::from(std::move(device));

	util::add_method<&DeviceProperties::property_getter>(*this, "__get");
}
auto Device::get_properties() -> Ref<DeviceProperties> {
	return util::new_object<DeviceProperties>(Ref<Device>{*this});
}
Device::Device(Ref<Context> &&ctx_, const char *syspath) {
	auto ctx = std::move(ctx_);        // Move the context object
	auto object_ref = Ref<Device>{*this};
	device = udev_device_new_from_syspath(ctx->context, syspath);

	// libudev keeps a ref to `udev` inside udev_device, so we do the same to
	// keep track of the udev context
	object_ref.raw_members()["__udev_context"] = Variant::from(std::move(ctx));

	util::add_method<&Device::get_properties>(*this, "__get_properties");
}

auto Module::get_or_create_context() -> Ref<Context> {
	auto object_ref = Ref<Module>{*this};
	auto context_proxy = object_ref.raw_members()["__udev_context"];
	std::optional<Variant> maybe_context_ref = context_proxy;
	if (maybe_context_ref.has_value()) {
		auto maybe_context = maybe_context_ref->to<WeakRef<Object>>().value().upgrade();
		if (maybe_context.has_value()) {
			return *std::move(*maybe_context).downcast<Context>();
		}
	}
	auto context = util::new_object<Context>();
	auto weak_context = context.downgrade();
	context_proxy = Variant::from(std::move(weak_context));
	return context;
}

auto Module::device_from_dev_node(std::string_view dev_node) -> Ref<Device> {
	auto context = get_or_create_context();
	auto *e = udev_enumerate_new(context->context);
	std::string dev_node_copy{dev_node.data(), dev_node.size()};
	udev_enumerate_add_match_property(e, "DEVNAME", dev_node_copy.c_str());
	udev_enumerate_scan_devices(e);

	auto *entry = udev_enumerate_get_list_entry(e);
	auto ret = util::new_object<Device>(std::move(context), udev_list_entry_get_name(entry));

	udev_enumerate_unref(e);
	return ret;
}
auto Module::search() -> Ref<Enumerator> {
	auto context = get_or_create_context();
	return util::new_object<Enumerator>(context->context);
}
/// udev
///
/// EXPORT: udev: deai:module
///
/// Interface to the udev Linux subsystem. This is very much work in progress.
auto di_new_udev(::deai::Ref<::deai::Core> &di) {
	auto module = util::new_object<Module>();
	util::add_method<&Module::device_from_dev_node>(module, "device_from_dev_node");
	util::add_method<&Module::search>(module, "search");
	return module;
}

DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	auto obj = di_new_udev(di);
	static_cast<void>(di->register_module("udev", std::move(obj).cast()));
}
}        // namespace
