di.os.env.DBUS_SESSION_BUS_PID = nil
di.os.env.DBUS_SESSION_BUS_ADDRESS = nil
di.os.env.DISPLAY = nil
--di.log.log_level = "debug"

unpack = table and table.unpack or unpack

local dbusl = di.spawn:run({"dbus-daemon", "--print-address=1", "--print-pid=2", "--session", "--fork"}, false)
local outlh
outlh = dbusl:on("stdout_line", function(l)
    -- remove new line
    if l == "" then
        return
    end
    print(l)
    outlh:stop()
    di.os.env.DBUS_SESSION_BUS_ADDRESS = l
end)
local errlh
errlh = dbusl:on("stderr_line", function(l)
    if l == "" then
        return
    end
    print(l)
    errlh:stop()
    di.os.env.DBUS_SESSION_BUS_PID = l
end)
local b
dbusl:once("exit", function()
    b = di.dbus.session_bus
    if b.errmsg then
        di:exit(1)
    end
    b = nil
    b = di.dbus.session_bus
    local o = b:get("org.freedesktop.DBus", "/org/freedesktop/DBus")

    -- Use non-existent method to test message serialization
    di.event:collect_promises({
        o:Introspect(),
        o:ListNames(),
        o:GetAllMatchRules(),
        o["org.dummy.Dummy"](o, {1,2,3}),
        o["org.dummy.Dummy"](o, {"asdf","qwer"}),
        o["org.dummy.Dummy"](o, 1),
        o["org.dummy.Dummy"](o, "asdf"),
        o["org.dummy.Dummy"]:call_with_signature(o, "iii", 1,2,3),
        o["org.dummy.Dummy"]:call_with_signature(o, "av", {1,2,3}),
    }):then_(function(results)
        for i, v in pairs(results) do
            is_err, reply = unpack(v);
            print(i, is_err, unpack(reply))
        end
    end)
    o = nil
end)
