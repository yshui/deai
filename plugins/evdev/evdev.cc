#include <deai/c++/deai.hh>

#include <fcntl.h>
#include <unistd.h>

#include <linux/input.h>
#include <linux/ioctl.h>

#include "common.h"

namespace {
struct InputId;
struct Device;
struct Module;
}        // namespace

namespace {
using namespace ::deai;

struct InputId {
	static constexpr const char type[] = "deai.plugin.evdev:InputId";
	ObjectBase base;
	/// Vendor
	///
	/// EXPORT: deai.plugin.evdev:InputId.vendor: :integer
	uint16_t vendor_;
	/// Product
	///
	/// EXPORT: deai.plugin.evdev:InputId.product: :integer
	uint16_t product_;
	/// Bus type
	///
	/// EXPORT: deai.plugin.evdev:InputId.bustype: :integer
	uint16_t bustype_;
	/// Version
	///
	/// EXPORT: deai.plugin.evdev:InputId.version: :integer
	uint16_t version_;

	[[nodiscard]] auto vendor() const -> int {
		return vendor_;
	}
	[[nodiscard]] auto product() const -> int {
		return product_;
	}

	[[nodiscard]] auto bustype() const -> int {
		return bustype_;
	}

	[[nodiscard]] auto version() const -> int {
		return version_;
	}

	InputId(uint16_t v, uint16_t p, uint16_t b, uint16_t ver)
	    : vendor_{v}, product_{p}, bustype_{b}, version_{ver} {
	}
	InputId(::input_id id) : InputId{id.vendor, id.product, id.bustype, id.version} {
	}
};

struct Device {
	static constexpr const char type[] = "deai.plugin.evdev:Device";
	ObjectBase base;
	int fd;

	[[nodiscard]] auto id() const -> Ref<Object>;
	[[nodiscard]] auto name() const -> Variant;

	Device(const std::string &dev_node) : fd(::open(dev_node.c_str(), O_RDONLY)) {
	}
	~Device() {
		close(fd);
	}
};

/// Device id
///
/// EXPORT: deai.plugin.evdev:Device.id: deai.plugin.evdev:InputId
auto Device::id() const -> Ref<Object> {
	::input_id id;
	if (::ioctl(fd, EVIOCGID, &id) < 0) {
		throw util::new_error("Failed to get device id information");
	}
	auto obj = util::new_object<InputId>(id);
	util::add_method<&InputId::vendor>(obj, "__get_vendor");
	util::add_method<&InputId::product>(obj, "__get_product");
	util::add_method<&InputId::bustype>(obj, "__get_bustype");
	util::add_method<&InputId::version>(obj, "__get_version");
	return std::move(obj).cast();
}

/// Device name
///
/// EXPORT: deai.plugin.evdev:Device.name: :string
auto Device::name() const -> Variant {
	std::vector<char> buf(80, 0);        // NOLINT
	while (true) {
		auto copied = ::ioctl(fd, EVIOCGNAME(buf.size()), buf.data());
		if (copied < 0) {
			throw util::new_error("Failed to get device name");
		}
		// Expand the buffer until the name fits
		if (static_cast<size_t>(copied) == buf.size()) {
			buf.resize(buf.size() * 2);
		} else {
			buf.resize(copied);
			// Pop the extra null terminator
			buf.pop_back();
			break;
		}
	}
	return Variant::from(std::string{buf.data(), buf.size()});
}

struct Module {
	static constexpr const char type[] = "deai.plugin.evdev:Module";
	ObjectBase base;
	/// Open a device node
	///
	/// EXPORT: evdev.open(path: :string): deai.plugin.evdev:Device
	auto device_from_dev_node(const std::string &dev_node) -> Ref<Device> {
		static_cast<void>(this);        // slient "this functino could be static" warning
		auto device = util::new_object<Device>(dev_node);
		if (device->fd < 0) {
			device["errmsg"] = Variant::from(std::string{"Failed to open device"});
		} else {
			util::add_method<&Device::id>(device, "__get_id");
			util::add_method<&Device::name>(device, "__get_name");
		}
		return device;
	}
};

/// evdev
///
/// EXPORT: evdev: deai:module
///
/// Interface to the Linux evdev subsystem.
auto di_new_evdev(::deai::Ref<::deai::Core> &di) {
	auto module = util::new_object<Module>();
	util::add_method<&Module::device_from_dev_node>(module, "open");
	return module;
}
DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	auto obj = di_new_evdev(di);
	static_cast<void>(di->register_module("evdev", std::move(obj).cast()));
}
}        // namespace
