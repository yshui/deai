--di.log.log_level = "debug"
local resolved_1 = false
local resolved_2 = false
a = di.event:new_promise()
a:then_(function(a)
    print(a)
    if a ~= 1 then
        di:exit(1)
    end
    resolved_1 = true
    return a+10
end)
:then_(function(b)
    if b ~= 11 then
        di:exit(1)
    end
    resolved_2 = true
    print(b)
end)

a:resolve(1)

a = di.event:new_promise()
a:resolve(1)
b = di.event:new_promise()
b:resolve(2)

local resolved_c = false
c = di.event:join_promises({a,b})
c:then_(function(t)
    if t[1] ~= 1 or t[2] ~= 2 then
        di:exit(1)
    end
    for i, v in pairs(t) do
        print(i, v)
    end
    resolved_c = true
end)

local resolved_any = false
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
    resolved_any = true
end)

local resolved_chain = false
a = di.event:new_promise()
b = di.event:new_promise()
a2 = a:then_(function(a)
    print("start of chained", a)
    return b
end)
a2:then_(function(a)
    if a ~= 4 then
        di:exit(1)
    end
    print("chain", a)
    resolved_chain = true
end)
a:resolve(1)
b:resolve(4)

di.event:timer(0.5):once("elapsed", function()
    print(resolved_1, resolved_2, resolved_c, resolved_any, resolved_chain)
    if not resolved_1 or not resolved_2 or not resolved_c or not resolved_any or not resolved_chain then
        di:exit(1)
    end
end)

unresolved = di.event:new_promise()
collectgarbage()
