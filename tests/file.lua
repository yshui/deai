di.load_plugin("./plugins/file/di_file.so")

md = di.spawn.run({"mkdir", "testdir"}, true)

md.on("exit", true, function()
w = di.file.watch({"testdir"})

function sigh(ev)
    return function(name, path)
        print("event: "..ev, name, path)
    end
end

events = {"create", "access", "attrib", "close-write", "close-nowrite",
"delete", "delete-self", "modify", "move-self", "open",
"moved-to", "moved-from"}

for _, i in pairs(events) do
    w.on(i, sigh(i))
end

fname = "./testdir/testfile"
f = di.log.file_target(fname, false)
f.write("Test")
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
        c = di.spawn.run(cmds[i], true)
        if i < #cmds then
            c.on("exit", true, run_one(i+1))
        else
            w.remove("testdir")
            w.stop()
        end
    end
end
run_one(1)()
end)
