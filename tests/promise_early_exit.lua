--di.log.log_level = "debug"
a = di.event:new_promise()
b = di.event:new_promise()
a2 = a:then_(function(a) return b end)
a2:then_(function(a)
    di:exit(0)
    print("chain", a)
end)
a:resolve(1)
b:resolve(4)

unresolved = di.event:new_promise()
collectgarbage()
