local t = di.event:timer(1)
di.event:timer(2):once("elapsed", function()
    print("elapsed2")
    t:once("elapsed", function()
        print("elapsed3")
    end)
end)



local t2 = di.event:timer(1)
lh = t2:on("elapsed", function() end)
lh:stop()
t2:once("elapsed", function()
    print("elapsed1")
end)
