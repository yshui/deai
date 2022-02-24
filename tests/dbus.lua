di.os.env.DBUS_SESSION_BUS_PID = nil
di.os.env.DBUS_SESSION_BUS_ADDRESS = nil
di.os.env.DISPLAY = nil

di.log.log_level = "debug"

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
function with_error(pending)
    if pending.errmsg then
        print(pending.errmsg)
        di:exit(1)
    end
    local errlh, replylh
    replylh = pending:signal("reply"):once("resolved", function(r)
        errlh:stop()
        print("Reply:", r)
    end)
    errlh = pending:signal("error"):once("resolved", function(e)
        replylh:stop()
        print(e)
    end)
end
function call_with_error(o, name, ...)
    t = o[name](o, ...)
    with_error(t)
end

function call_with_signature_and_error(o, name, sig, ...)
    t = o[name]:call_with_signature(o, sig, ...)
    with_error(t)
end

dbusl:once("exit", function()
    local b = di.dbus.session_bus
    if b.errmsg then
        print(b.errmsg)
        di:exit(1)
    end
    b = nil
    local o = di.dbus.session_bus:get("org.freedesktop.DBus", "/org/freedesktop/DBus")
    o:Introspect():once("reply", function(s)
        print(s)
    end)
    o:ListNames():once("reply", function(s)
        for _, i in pairs(s) do
            print(i)
        end
    end)
    o:GetAllMatchRules():once("reply", function(s)
        print(s)
    end)

    -- Use non-existent method to test message serialization
    call_with_error(o, "org.dummy.Dummy", {1,2,3})
    call_with_error(o, "org.dummy.Dummy", {"asdf","qwer"})
    call_with_error(o, "org.dummy.Dummy", 1)
    call_with_error(o, "org.dummy.Dummy", "asdf")
    call_with_signature_and_error(o, "org.dummy.Dummy", "iii", 1,2,3)
    call_with_signature_and_error(o, "org.dummy.Dummy", "av", {1,2,3})
    o = nil
    collectgarbage("collect")
end)
