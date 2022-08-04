print(di.os.env.PATH)
di.os.env.PATH = "/non-existent"
print(di.os.env.PATH)

e = di.spawn:run({"ls"}, false)
e:once("exit", function(ec, sig)
    print(ec, sig)
    assert(ec == 1)
end)
e = nil

di.os.env.PATH = nil
assert(di.os.env.PATH == nil)

files = di.os:listdir(".")
assert(#files ~= 0)
