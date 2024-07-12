scripts = di.os:listdir(di.DI_PLUGIN_INSTALL_DIR .. "/lua")

for _, s in pairs(scripts) do
    if s:match("%.lua$") and s ~= "builtins.lua" then
        print("Loading " .. s)
        dofile(di.DI_PLUGIN_INSTALL_DIR .. "/lua/" .. s)
    end
end
