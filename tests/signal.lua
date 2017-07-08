a = {ev = di.new_signal("int")}

di.register_module("test_a", a)
di.test_a.on("ev", function(a, i)
    print(a, i)
    assert(i == 20)
    di.quit()
end)
di.test_a.emit("ev", 20)
