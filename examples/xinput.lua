function apply_xi_settings(dev)
    if dev.type == "touchpad" then
        dev.set_prop("libinput Tapping Enabled", 1)
    end
    if dev.use == "pointer" then
        dev.set_prop("libinput Natural Scrolling Enabled", 1)
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
end
