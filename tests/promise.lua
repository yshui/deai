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
