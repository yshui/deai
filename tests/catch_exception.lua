local function test()
	di.misc:difference_constraints({
		"asdf"
	})
end
local function starts_with(str, start)
   return str:sub(1, #start) == start
end
local status, err = pcall(test)
print(err.errmsg)
if not starts_with(err.errmsg, "Array element type mismatch, string cannot be converted into ") then
	di:exit(1)
end
