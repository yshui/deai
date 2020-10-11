lua_driver = shared_library('test_lua', 'test_lua.c', c_args: base_c_args, name_prefix: '', include_directories: incs)
executable('cs', 'child_spawner.c')
test_cases = [
  'env.lua',
  'quit.lua',
  'remote_call.lua',
  'signal.lua',
  'timer.lua',
  'timer2.lua',
  'dbus.lua',
  'file.lua',
  'kill.lua',
  'x.lua',
  'log.lua',
  'weak.lua',
  'roots.lua',
]
foreach t : test_cases
  test(t, deai_exe, args:
       ['load_plugin',
        's:' + lua_driver.full_path(),
        '--',
        meson.current_source_dir() / t
       ], timeout: 3)
endforeach

core_test_cases = [
  'conversion_test.c',
  'anonymous_root_test.c',
  'drop_event_source_when_listener_is_attached.c',
  'c++_test.cc',
]

foreach t : core_test_cases
  test_so = shared_library(t.underscorify(), t, c_args: base_c_args, cpp_args: base_cpp_args, name_prefix: '', include_directories: incs)
  test(t.underscorify()+'_test', deai_exe, args: ['load_plugin', 's:' + test_so.full_path()])
endforeach
