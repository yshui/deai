di.spawn.run({"tests/cs", "20"}, true)
t = di.event.timer(1)
t.on("elapsed", function()
    di.quit()
end)