print(di.os.env.PATH)
di.os.env.PATH = "/non-existent"
print(di.os.env.PATH)

e = di.spawn.run({"ls"}, true)
e.on("exit", function(e, ec, sig)
    print(ec, sig)
end)
