-- Make sure it's possible to set member from both deai and lua
a = {}
di:register_module("a", a)

di.a.a = "asdf"
if a.a ~= di.a.a then
    di:exit(1)
end

a.b= 2
if a.b ~= di.a.b then
    di:exit(1)
end
