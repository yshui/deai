local ret = {
  ["a"] = 1,
  ["b"] = "asdf",
}
local mt = {
  __tostring = function(t)
    return "this is a tostring test"
  end,
}
setmetatable(ret, mt)
return ret
