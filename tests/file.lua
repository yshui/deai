di:load_plugin("./plugins/file/di_file.so")

unpack = table.unpack or unpack
md = di.spawn:run({"mkdir", "testdir"}, true)

md:once("exit", function()
w = di.file:watch({"testdir"})

function sigh(ev)
    return function(name, path)
        print("event: "..ev, name, path)
    end
end

events = {"create", "access", "attrib", "close-write", "close-nowrite",
"delete", "delete-self", "modify", "move-self", "open",
"moved-to", "moved-from"}

listen_handles = {}
for _, i in pairs(events) do
    handle = w:on(i, sigh(i))
    handle:auto_stop(1)
    table.insert(listen_handles, handle)
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
            c:once("exit", run_one(i+1))
        else
            w:remove("testdir")
            w = nil
            listen_handles = nil
            collectgarbage()
        end
    end
end
run_one(1)()
end)
md = nil
collectgarbage()
