di.spawn:run({"Xvfb", ":101", "-screen", "0", "1600x1200x24+32"}, true)
di.spawn:run({"Xvfb", ":102", "-screen", "0", "1600x1200x24+32"}, true)
di.os.env.DISPLAY=":101"

local listen_handles = {}
table.insert(listen_handles, di.event:timer(0.2):on("elapsed", function()
    -- wait a while for Xvfb to start
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
    local new_device = false
    table.insert(listen_handles, xi:on("new-device", function(dev)
        new_device = true
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

    local key_pressed = false
    local view_changed = false
    local output_changed = false
    table.insert(listen_handles, o.key:new({"mod4"}, "d", false):on("pressed", function()
        print("pressed")
        key_pressed = true
    end))

    -- test the new-device event
    di.spawn:run({"xinput", "create-master", "b"}, true)
    -- test key events
    di.spawn:run({"xdotool", "key", "super+d"}, true)

    table.insert(listen_handles, o.randr:on("view-change", function()
        print("view-change")
        view_changed = true
    end))
    table.insert(listen_handles, o.randr:on("output-change", function()
        print("output-change")
        output_changed = true
    end))

    print("Modes:")
    modes = o.randr.modes
    for _, i in pairs(modes) do
        print(i.width, i.height, i.id)
    end
    outs = o.randr.outputs
    for _, i in pairs(outs) do
        print(i.name, i.backlight, i.max_backlight)
        local vc = i.current_view.config
        if vc then
            print(vc.x, vc.y, vc.width, vc.height, vc.rotation, vc.reflection)
            i.current_view.config = {x=vc.x, y=vc.y, rotation=vc.rotation, reflection=vc.reflection, mode=modes[1].id}
        end
        if i.current_view.outputs then
            print(i.current_view.outputs[1].name)
        end
    end

    di.spawn:run({"xrandr", "--output", "screen", "--off"}, true)

    table.insert(listen_handles, di.event:timer(1):on("elapsed", function()
        devs = xi.devices
        for _, d in pairs(devs) do
            print(string.format("device: %s %s %s %d", d.type, d.use, d.name, d.id))
        end

        o:disconnect()
        o = di.xorg:connect_to(":102")
        if o.errmsg then
            print(o.errmsg)
            di:exit(1)
        else
            o:disconnect()
        end
        if not key_pressed then
            print("key pressed not received")
            di:exit(1)
        end
        if not output_changed then
            print("output changed not received")
            di:exit(1)
        end
        if not view_changed then
            print("view changed not received")
            di:exit(1)
        end
        if not new_device then
            print("new device not received")
            di:exit(1)
        end
        for _, lh in pairs(listen_handles) do
            lh:stop()
        end
    end))
end))
