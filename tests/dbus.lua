di.load_plugin("./plugins/dbus/di_dbus.so")
di.env.DBUS_SESSION_BUS_PID = nil
di.env.DBUS_SESSION_BUS_ADDRESS = nil
di.env.DISPLAY = nil
local dbusl = di.spawn.run({"dbus-daemon", "--print-address=1", "--print-pid=2", "--session", "--fork"}, false)
dbusl.on("stdout_line", function(l)
    -- remove new line
    if l == "" then
        return
    end
    print(l)
    di.env.DBUS_SESSION_BUS_ADDRESS = l
end)
dbusl.on("stderr_line", function(l)
    if l == "" then
        return
    end
    print(l)
    di.env.DBUS_SESSION_BUS_PID = l
end)
dbusl.on("exit", function()
    b = di.dbus.session_bus
    if b.errmsg then
        print(b.errmsg)
        di.exit(1)
    end
    o = di.dbus.session_bus.get("org.freedesktop.DBus", "/org/freedesktop/DBus")
    o.Introspect().on("reply", function(s)
        print(s)
    end)
    o.ListNames().on("reply", function(s)
        for _, i in pairs(s) do
            print(i)
        end
    end)
    o.GetAllMatchRules().on("reply", function(s)
        print(s)
    end)

    -- Use non-existent method to test message serialization
    o.Dummy({1,2,3}).on("error", function(e)
        print(e)
    end)
    o.Dummy(1).on("error", function(e)
        print(e)
    end)
    o.Dummy("asdasf").on("error", function(e)
        print(e)
    end)
end)
