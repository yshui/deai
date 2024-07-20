a = di.event:new_promise()
local caught = false
local resolved = false
a:then_(function(x) end)
:catch(function(e)
	print("Caught:", e);
	if tostring(e) ~= "test" then
		di:exit(1)
	end
	caught = true
	return 10
end)
:then_(function(v)
	if v ~= 10 then
		di:exit(1)
	end
	resolved = true
	print(v)
end)

local error = { error = "test" }
setmetatable(error, { __tostring = function(self) return self.error end })
a:reject(error)

di.event:timer(0.5):on("elapsed", function()
	print(caught, resolved)
	if not caught or not resolved then
		di:exit(1)
	end
end)
