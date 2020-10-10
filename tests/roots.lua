obj = di:create_di_object()
di:add_root("test_root", obj)
weak = obj:weakref()
obj = nil

collectgarbage("collect")

-- check if a root kept the object alive
obj = weak:upgrade()
assert(obj ~= nil)

di:remove_root("test_root")
obj = nil

collectgarbage("collect")

-- check if the object is dead after removing the root
obj = weak:upgrade()
assert(obj == nil)

obj = di:create_di_object()
di:add_root("test_root", obj)
weak = obj:weakref()
obj = nil

collectgarbage("collect")

di:clear_roots()

-- check if clear_roots works
obj = weak:upgrade()
assert(obj == nil)
