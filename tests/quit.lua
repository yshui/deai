-- create a event source that would normally keep deai running
fde = di.event:fdevent(0);
fde:on("read", function()
    print("readable")
end)
di:quit()
