di:load_plugin("./plugins/file/di_file.so")

md = di.spawn:run({"mkdir", "testdir"}, true)
listen_handles = {}

table.insert(listen_handles, md:on("exit", function()
w = di.file:watch({"testdir"})

function sigh(ev)
    return function(name, path)
        print("event: "..ev, name, path)
    end
end

events = {"create", "access", "attrib", "close-write", "close-nowrite",
"delete", "delete-self", "modify", "move-self", "open",
"moved-to", "moved-from"}

for _, i in pairs(events) do
    table.insert(listen_handles, w:on(i, sigh(i)))
end

fname = "./testdir/testfile"
f = di.log:file_target(fname, false)
f:write("Test")
f = nil
collectgarbage()
cmds = {
    {"touch", fname},
    {"cat", fname},
    {"mv", fname, fname.."1"},
    {"rm", fname.."1"},
    {"rmdir", "./testdir"},
    {"echo", "finished"}
}

function run_one(i)
    return function()
        print("running ", unpack(cmds[i]))
        c = di.spawn:run(cmds[i], true)
        if i < #cmds then
            table.insert(listen_handles, c:on("exit", run_one(i+1)))
        else
            w:remove("testdir")
            w:stop()
            w = nil
            for _, lh in pairs(listen_handles) do
                -- Stop all listeners, so the callback functions can be freed,
                -- which in turn frees the lua script object
                lh:stop()
            end
            collectgarbage()
        end
    end
end
run_one(1)()
end))
