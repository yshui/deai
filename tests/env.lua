print(di.os.env.PATH)
di.os.env.PATH = "/non-existent"
print(di.os.env.PATH)

e = di.spawn:run({"ls"}, true)
handle = e:on("exit", function(ec, sig)
    handle:stop()
    print(ec, sig)
    assert(ec == 1)
end)

di.os.env.PATH = nil
assert(di.os.env.PATH == nil)
