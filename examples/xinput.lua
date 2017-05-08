function apply_xi_settings(dev)
    p = dev.props
    if dev.type == "touchpad" then
        p["libinput Tapping Enabled"] = {1}
        p["libinput Tapping Button Mapping Enabled"] = {0,1}
    end
    if dev.use == "pointer" then
        p["libinput Natural Scrolling Enabled"] = {1}
    end
end
xc = di.xorg.connect()
xi = xc.xinput
xi.on("new-device", function(dev)
    print("new device", dev.type, dev.use, dev.name, dev.id)
    apply_xi_settings(dev)
end)

devs = xi.devices;
for i, v in ipairs(devs) do
    print(i, v.id, v.name, v.type, v.use)
    apply_xi_settings(v)

    p = v.props
    pid = p["Device Product ID"]
    if pid ~= nil then
        print(pid[1], pid[2])
    end
end
