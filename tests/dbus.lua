di.os.env.DBUS_SESSION_BUS_PID = nil
di.os.env.DBUS_SESSION_BUS_ADDRESS = nil
di.os.env.DISPLAY = nil
--di.log.log_level = "debug"

function signal_promise(obj, signal)
    local promise = di.event:new_promise()
    obj:once(signal, function(v)
        promise:resolve(v)
    end)
    return promise
end

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
function with_error(pending)
    if pending.errmsg then
        di:exit(1)
    end
    reply_promise = signal_promise(pending, "reply"):then_(function(r)
        assert(false)
        print(r)
        return r
    end)
    error_promise = signal_promise(pending, "error"):then_(function(r)
        return "Error: "..r
    end)
    return di.event:any_promise({reply_promise, error_promise})
end
function call_with_error(o, name, ...)
    t = o[name](o, ...)
    return with_error(t)
end

function call_with_signature_and_error(o, name, sig, ...)
    t = o[name]:call_with_signature(o, sig, ...)
    return with_error(t)
end

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
        signal_promise(o:Introspect(), "reply"),
        signal_promise(o:ListNames(), "reply"),
        signal_promise(o:GetAllMatchRules(), "reply"),
        call_with_error(o, "org.dummy.Dummy", {1,2,3}),
        call_with_error(o, "org.dummy.Dummy", {"asdf","qwer"}),
        call_with_error(o, "org.dummy.Dummy", 1),
        call_with_error(o, "org.dummy.Dummy", "asdf"),
        call_with_signature_and_error(o, "org.dummy.Dummy", "iii", 1,2,3),
        call_with_signature_and_error(o, "org.dummy.Dummy", "av", {1,2,3}),
    }):then_(function(results)
        for i, v in pairs(results) do
            print(i, v)
        end
    end)
    o = nil
end)
