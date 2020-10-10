obj = di:create_di_object()
roots = di.roots:upgrade()
roots:add("test_root", obj)
weak = obj:weakref()
obj = nil

collectgarbage("collect")

-- check if a root kept the object alive
obj = weak:upgrade()
assert(obj ~= nil)

roots:remove("test_root")
obj = nil

collectgarbage("collect")

-- check if the object is dead after removing the root
obj = weak:upgrade()
assert(obj == nil)

obj = di:create_di_object()
roots:add("test_root", obj)
weak = obj:weakref()
obj = nil

collectgarbage("collect")

roots:clear()

-- check if clear_roots works
obj = weak:upgrade()
assert(obj == nil)
