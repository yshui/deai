di:load_plugin("./plugins/dbus/di_dbus.so")
di.os.env.DBUS_SESSION_BUS_PID = nil
di.os.env.DBUS_SESSION_BUS_ADDRESS = nil
di.os.env.DISPLAY = nil
local dbusl = di.spawn:run({"dbus-daemon", "--print-address=1", "--print-pid=2", "--session", "--fork"}, false)
local outlh = dbusl:on("stdout_line", function(l)
    -- remove new line
    if l == "" then
        return
    end
    print(l)
    di.os.env.DBUS_SESSION_BUS_ADDRESS = l
end)
local errlh = dbusl:on("stderr_line", function(l)
    if l == "" then
        return
    end
    print(l)
    di.os.env.DBUS_SESSION_BUS_PID = l
end)
function call_with_error(o, name, ...)
    t = o[name](o, ...)
    if t.errmsg then
        print(t.errmsg)
        di:exit(1)
    end
    local errlh, replylh
    replylh = t:on("reply", function(_)
        errlh:stop()
        replylh:stop()
    end)
    errlh = t:on("error", function(e)
        errlh:stop()
        replylh:stop()
        print(e)
    end)
end

local exitlh
exitlh = dbusl:on("exit", function()
    outlh:stop()
    errlh:stop()
    exitlh:stop()

    local b = di.dbus.session_bus
    if b.errmsg then
        print(b.errmsg)
        di:exit(1)
    end
    b = nil
    local o = di.dbus.session_bus:get("org.freedesktop.DBus", "/org/freedesktop/DBus")
    local introspectlh, listnamelh, matchlh, timerlh
    introspectlh = o:Introspect():on("reply", function(s)
        introspectlh:stop()
        print(s)
    end)
    listnamelh = o:ListNames():on("reply", function(s)
        listnamelh:stop()
        for _, i in pairs(s) do
            print(i)
        end
    end)
    matchlh = o:GetAllMatchRules():on("reply", function(s)
        matchlh:stop()
        print(s)
    end)

    timerlh = di.event:timer(2):on('elapsed', function()
        timerlh:stop()
        di:quit()
    end)

    -- Use non-existent method to test message serialization
    call_with_error(o, "org.dummy.Dummy", {1,2,3})
    call_with_error(o, "org.dummy.Dummy", {"asdf","qwer"})
    call_with_error(o, "org.dummy.Dummy", 1)
    call_with_error(o, "org.dummy.Dummy", "asdf")
    o = nil
    collectgarbage("collect")
end)
