--di.log.log_level = "debug"
a = di.event:new_promise()
a:then_(function(a)
    print(a)
    if a ~= 1 then
        di:exit(1)
    end
    return a+10
end)
:then_(function(b)
    if b ~= 11 then
        di:exit(1)
    end
    print(b)
end)

a:resolve(1)

a = di.event:new_promise()
a:resolve(1)
b = di.event:new_promise()
b:resolve(2)

c = di.event:collect_promises({a,b})
c:then_(function(t)
    if t[1] ~= 1 or t[2] ~= 2 then
        di:exit(1)
    end
    for i, v in pairs(t) do
        print(i, v)
    end
end)

a = di.event:new_promise()
a:resolve(5)
a2 = a:then_(function(a) return a+10 end)
b = di.event:new_promise()
c = di.event:any_promise({a2,b})
c:then_(function(t)
    if t ~= 15 then
        di:exit(1)
    end
    print(t)
end)

unresolved = di.event:new_promise()
collectgarbage()
