di:load_plugin("./plugins/xorg/di_xorg.so")
di.spawn:run({"Xvfb", ":1", "-screen", "0", "1600x1200x24+32"}, true)
di.spawn:run({"Xvfb", ":2", "-screen", "0", "1600x1200x24+32"}, true)
di.os.env.DISPLAY=":1"

local listen_handles = {}
table.insert(listen_handles, di.event:timer(0.2):on("elapsed", function()
    collectgarbage("collect")

    -- wait a awhile for Xvfb to start
    o = di.xorg:connect()
    assert(o.asdf == nil)
    if o.errmsg then
        print(o.errmsg)
        di:exit(1)
        return
    end
    print(o.xrdb)
    o.xrdb = "Xft.dpi:\t192"
    print(o.xrdb)

    o.keymap = { layout = "us", options = "ctrl:nocaps" }
    xi = o.xinput
    table.insert(listen_handles, xi:on("new-device", function(dev)
        print(string.format("new device %s %s %s %d", dev.type, dev.use, dev.name, dev.id))
        print("enabled:", dev.props["Device Enabled"])
        if dev.use == "pointer" then
            dev.props["Coordinate Transformation Matrix"] = {2, 0, 0, 0, 2, 0, 0, 0, 1}
            if table.unpack then
                print("matrix:", table.unpack(dev.props["Coordinate Transformation Matrix"]))
            else
                print("matrix:", unpack(dev.props["Coordinate Transformation Matrix"]))
            end
        end
    end))

    table.insert(listen_handles, o.key:new({"mod4"}, "d", false):on("pressed", function()
        print("pressed")
    end))

    -- test the new-device event
    di.spawn:run({"xinput", "create-master", "b"}, true)
    -- test key events
    di.spawn:run({"xdotool", "key", "super+d"}, true)

    table.insert(listen_handles, o.randr:on("view-change", function()
        print("view-change")
    end))
    table.insert(listen_handles, o.randr:on("output-change", function()
        print("output-change")
    end))

    print("Modes:")
    modes = o.randr.modes
    for _, i in pairs(modes) do
        print(i.width, i.height, i.id)
    end
    outs = o.randr.outputs
    for _, i in pairs(outs) do
        print(i.name, i.backlight, i.max_backlight)
        local vc = i.view.config
        if vc then
            print(vc.x, vc.y, vc.width, vc.height, vc.rotation, vc.reflection)
            i.view.config = {x=vc.x, y=vc.y, rotation=vc.rotation, reflection=vc.reflection, mode=modes[1].id}
        end
        if i.view.outputs then
            print(i.view.outputs[1].name)
        end
    end

    di.spawn:run({"xrandr", "--output", "screen", "--off"}, true)

    table.insert(listen_handles, di.event:timer(1):on("elapsed", function()
        collectgarbage("collect")
        devs = xi.devices
        for _, d in pairs(devs) do
            print(string.format("device: %s %s %s %d", d.type, d.use, d.name, d.id))
        end

        o:disconnect()
        o = di.xorg:connect_to(":2")
        if o.errmsg then
            print(o.errmsg)
        end
        o:disconnect()
        for _, lh in pairs(listen_handles) do
            lh:stop()
        end
    end))
end))
