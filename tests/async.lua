function signal_promise(obj, signal)
    local promise = di.event:new_promise()
    obj:once(signal, function(v)
        promise:resolve(v)
    end)
    return promise
end

function run_async(func)
    local co = coroutine.create(func)
    local function turn(...)
        local _, p = coroutine.resume(co, ...)
        if p and p.then_ then
            p:then_(function(data)
                turn(data)
            end)
        end
    end
    turn()
end

function await(promise)
    return coroutine.yield(promise)
end

function test()
    local timer, p, a
    timer = di.event:timer(1)
    p = signal_promise(timer, "elapsed")
    a = await(p)
    print("continued", a)
    timer = di.event:timer(1)
    p = signal_promise(timer, "elapsed")
    a = await(p)
    print("continued2", a)
end

run_async(test)
