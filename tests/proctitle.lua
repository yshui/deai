di.os.env.TEST = "1"
t = di.event:periodic(0.1, 0.05)
count = 0
t:on("triggered", function()
    count = count+1
    if count == 10 then
        di:quit()
    end
    di.proctitle = tostring(count)
end)
