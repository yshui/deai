obj = di.lua:as_di_object({["a"] = true})
di.roots:add("test_root", obj)
weak = obj:weakref()
obj = nil

collectgarbage("collect")

-- check if a root kept the object alive
obj = weak:upgrade()
assert(obj ~= nil)

di.roots:remove("test_root")
obj = nil

collectgarbage("collect")

-- check if the object is dead after removing the root
obj = weak:upgrade()
assert(obj == nil)

obj = di.lua:as_di_object({["b"] = true})
di.roots:add("test_root", obj)
weak = obj:weakref()
obj = nil

collectgarbage("collect")

di.roots:clear()

-- check if clear_roots works
obj = weak:upgrade()
assert(obj == nil)
