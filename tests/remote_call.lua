local called = false
obj = {
    func = function(mod)
        di:log("error", "test\n")
        called = true
    end
}

di:register_module("test_a", obj)
di.test_a:func()

-- Had to do this to eliminate the reference cycle
-- lua_state -> di -> lua_ref -> lua_state
di.test_a = nil
if not called then
    di:exit(1)
end
