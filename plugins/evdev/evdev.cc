#include <deai/c++/deai.hh>

#include <fcntl.h>
#include <unistd.h>

#include <linux/input.h>
#include <linux/ioctl.h>

#include "common.h"

namespace {
using namespace ::deai::c_api;
using namespace ::deai;

struct Device {
	static constexpr const char *type [[maybe_unused]] = "deai.plugin.evdev:Device";
	int fd;

	[[nodiscard]] auto id() const -> Ref<Object>;
	[[nodiscard]] auto name() const -> Variant;

	Device(const std::string &dev_node) : fd(::open(dev_node.c_str(), O_RDONLY)) {
		auto object_ref = util::unsafe_to_object_ref(*this);
		if (fd < 0) {
			object_ref["errmsg"] =
			    Variant::from(std::string{"Failed to open device"});
		} else {
			util::add_method<&Device::id>(*this, "__get_id");
			util::add_method<&Device::name>(*this, "__get_name");
		}
	}
	~Device() {
		close(fd);
	}
};

struct InputId {
private:
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

public:
	static constexpr const char *type [[maybe_unused]] = "deai.plugin.evdev:"
	                                                     "InputId";
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
		util::add_method<&InputId::vendor>(*this, "__get_vendor");
		util::add_method<&InputId::product>(*this, "__get_product");
		util::add_method<&InputId::bustype>(*this, "__get_bustype");
		util::add_method<&InputId::version>(*this, "__get_version");
	}
	InputId(::input_id id) : InputId{id.vendor, id.product, id.bustype, id.version} {
	}
};

/// Device id
///
/// EXPORT: deai.plugin.evdev:Device.id: deai.plugin.evdev:InputId
auto Device::id() const -> Ref<Object> {
	::input_id id;
	if (::ioctl(fd, EVIOCGID, &id) < 0) {
		return di_new_error("Failed to get device id information");
	}
	return util::new_object<InputId>(id);
}

/// Device name
///
/// EXPORT: deai.plugin.evdev:Device.name: :string
auto Device::name() const -> Variant {
	std::vector<char> buf(80, 0);        // NOLINT
	while (true) {
		auto copied = ::ioctl(fd, EVIOCGNAME(buf.size()), buf.data());
		if (copied < 0) {
			return Variant::from(di_new_error("Failed to get device name"));
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
private:
public:
	static constexpr const char *type [[maybe_unused]] = "deai.plugin.evdev:Module";
	/// Open a device node
	///
	/// EXPORT: evdev.open(path: :string): deai.plugin.evdev:Device
	auto device_from_dev_node(const std::string &dev_node) -> Ref<Object> {
		static_cast<void>(this);        // slient "this functino could be static" warning
		return util::new_object<Device>(dev_node);
	}
};

/// evdev
///
/// EXPORT: evdev: deai:module
///
/// Interface to the Linux evdev subsystem.
auto di_new_evdev(::deai::Ref<::deai::Core> &di) {
	auto obj = util::new_object<Module>();
	auto &module = util::unsafe_to_inner<Module>(obj);
	util::add_method<&Module::device_from_dev_node>(module, "open");
	return obj;
}
DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	auto obj = di_new_evdev(di);
	static_cast<void>(di->register_module("evdev", obj));
	return 0;
}
}        // namespace
