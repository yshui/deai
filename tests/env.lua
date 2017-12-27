print(di.env.PATH)
di.env.PATH = "/non-existent"
print(di.env.PATH)

e = di.spawn.run({"ls"}, true)
e.on("exit", function(e, ec, sig)
    print(ec, sig)
end)
