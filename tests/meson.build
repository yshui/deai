shared_library('test_lua', 'test_lua.c', c_args: base_c_args, name_prefix: '', include_directories: incs)
test_cases = ['env.lua', 'quit.lua', 'remote_call.lua', 'signal.lua', 'timer.lua', 'timer2.lua', 'dbus.lua']
foreach t : test_cases
  test(t, deai_exe, args: ['load_plugin', 's:tests/test_lua.so', '--', meson.current_source_dir()+'/'+t], workdir: meson.current_build_dir()+'/..')
endforeach
