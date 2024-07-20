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
local resolved_1 = false
local resolved_2 = false
local resolved_3 = false
local b
dbusl:once("exit", function()
    b = di.dbus.session_bus
    local o = b:get("org.freedesktop.DBus", "/org/freedesktop/DBus", "")
    local o3 = b:get("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus")
    local o2 = b:get("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.dummy")

    -- Use non-existent method to test message serialization
    di.event:join_promises({
        o:Introspect(),
        o:ListNames(),
        o:GetAllMatchRules(),
        o2:Dummy({1,2,3}),
        o2:Dummy({"asdf","qwer"}),
        o2:Dummy(1),
        o2:Dummy("asdf"),
        o2.Dummy:call_with_signature(o2, "iii", 1,2,3),
        o2.Dummy:call_with_signature(o2, "av", {1,2,3}),
    }):then_(function(results)
        for i, v in pairs(results) do
            print(i, v)
        end
        resolved_1 = true
    end)
    o2:get("DummyProp"):then_(function(e)
        if e.error == nil then
            di:exit(1)
        end
        print("Get DummyProp", e.error)
        resolved_2 = true
    end)
    o3:get("Features"):then_(function(e)
        if type(e) ~= "table" then
            di:exit(1)
        end
        print(unpack(e))
        resolved_3 = true
    end)
end)

di.event:timer(0.6):once("elapsed", function()
    print(resolved_1, resolved_2, resolved_3)
    if not resolved_1 or not resolved_2 or not resolved_3 then
        di:exit(1)
    end
end)
