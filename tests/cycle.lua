di.log.log_level = "debug"
p = di.event:new_promise()
p:then_(function() print("asdf") end)
