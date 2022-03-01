t = di.event:periodic(0.1, 0.05)
count = 0
handle = t:on("triggered", function()
    count = count+1
    if count == 10 then
        handle:stop()
        handle = nil
    end
    print(count)
end)
