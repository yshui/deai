shared_library('test_lua', 'test_lua.c', c_args: base_c_args, name_prefix: '', include_directories: incs)
executable('cs', 'child_spawner.c')
test_cases = ['env.lua', 'quit.lua', 'remote_call.lua', 'signal.lua', 'timer.lua', 'timer2.lua', 'dbus.lua', 'file.lua', 'kill.lua', 'x.lua', 'log.lua']
foreach t : test_cases
  test(t, deai_exe, args:
       ['load_plugin',
        's:' + meson.current_build_dir() / 'test_lua.so',
        '--',
        meson.current_source_dir() / t
       ], timeout: 3)
endforeach
