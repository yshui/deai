di.dbus
  .session_bus
  .get("org.freedesktop.DBus", "/org/freedesktop/DBus")
  .Introspect()
  .on("reply", function(s)
    print(s)
end)
collectgarbage()
