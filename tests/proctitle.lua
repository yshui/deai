di.os.env.TEST = "1"
t = di.event:periodic(0.1, 0.05)
count = 0
lh = t:on("triggered", function()
    count = count+1
    if count == 10 then
        lh:stop()
    end
    di.proctitle = tostring(count)
end)
t = nil
collectgarbage()
