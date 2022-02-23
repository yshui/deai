function run_unit_tests(obj)
    if obj.run_unit_tests ~= nil then
        obj:run_unit_tests()
    end
end

run_unit_tests(di.dbus)
