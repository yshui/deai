assert(di.udev ~= nil)


obj = di.udev:device_from_dev_node("/dev/dri/card0")
assert(obj ~= nil)

print(obj.properties["SUBSYSTEM"])

e = di.udev:search()
