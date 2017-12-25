bus = di.dbus.session_bus;
obj = bus.get("org.freedesktop.DBus", "/org/freedesktop/DBus")
obj.Introspect()
   .on("reply", function(s)
    print(s)
end)
obj.GetNameOwner("org.freedesktop.DBus").on("reply", function(s)
    print(s)
end)
obj = nil
bus = nil

collectgarbage()
