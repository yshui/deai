di.load_plugin("./plugins/dbus/di_dbus.so")
local dbusl = di.spawn.run({"dbus-daemon", "--print-address", "--session", "--fork"}, false)
dbusl.on("stdout_line", function(l)
    print(l)
    di.env.DBUS_SESSION_BUS_ADDRESS = l
    di.env.DBUS_SESSION_BUS_PID = nil
    di.env.DBUS_SESSION_BUS_WINDOWID = nil
end)
dbusl.on("exit", function()
    b = di.dbus.session_bus
    if b.errmsg then
        print(b.errmsg)
        di.exit(1)
    end
    di.dbus.session_bus.get("org.freedesktop.DBus", "/org/freedesktop/DBus").Introspect().on("reply", function(s)
        print(s)
        collectgarbage()
    end)
end)
