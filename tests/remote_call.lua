obj = {
    func = function()
        di.log("error", "test\n")
    end
}

di.register_module("test_a", obj)
di.test_a.func()
di.quit()
