a = {}

di:register_module("test_a", a)
di.test_a:once("ev", function(i)
    print(i)
    assert(i == 20)
    di:quit()
end)
di.test_a:emit("ev", 20)
