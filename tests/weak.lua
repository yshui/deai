obj = di.create_di_object()
weak_obj = obj.weakref()
obj2 = weak_obj.upgrade()
assert(obj2)
obj2 = nil
obj = nil
collectgarbage("collect")

obj3 = weak_obj.upgrade()
assert(not obj3)

-- deai should exit after this
