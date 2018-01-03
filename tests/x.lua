di.load_plugin("./plugins/xorg/di_xorg.so")
di.spawn.run({"Xvfb", ":1", "-screen", "0", "1600x1200x24+32"}, true)
di.spawn.run({"Xvfb", ":2", "-screen", "0", "1600x1200x24+32"}, true)
di.env.DISPLAY=":1"

di.event.timer(0.2).on("elapsed", true, function()
    -- wait a awhile for Xvfb to start
    o = di.xorg.connect()
    if o.errmsg then
        print(o.errmsg)
        di.exit(1)
        return
    end
    print(o.xrdb)
    o.xrdb = "Xft.dpi:\t192"
    print(o.xrdb)

    xi = o.xinput
    xi.on("new-device", function(dev)
        print(string.format("new device %s %s %s %d", dev.type, dev.use, dev.name, dev.id))
    end)

    o.key.new({"mod4"}, "d", false).on("pressed", function()
        print("pressed")
    end)

    -- test the new-device event
    di.spawn.run({"xinput", "create-master", "b"}, true)
    -- test key events
    di.spawn.run({"xdotool", "key", "super+d"}, true)

    o.randr.on("view-change", function()
        print("view-change")
    end)
    o.randr.on("output-change", function()
        print("output-change")
    end)

    outs = o.randr.outputs
    for _, i in pairs(outs) do
        print(i.name, i.backlight, i.max_backlight)
        vc = i.view.config
        print(vc.x, vc.y, vc.width, vc.height, vc.rotation, vc.reflection)
        i.view.config = vc
        print(i.view.outputs[1].name)
    end
    print("Modes:")
    modes = o.randr.modes
    for _, i in pairs(modes) do
        print(i.width, i.height, i.id)
    end

    di.event.timer(1).on("elapsed", true, function()
        o.disconnect()
        o = di.xorg.connect_to(":2")
        if o.errmsg then
            print(o.errmsg)
            di.exit(1)
            return
        end
        di.quit()
    end)
end)
