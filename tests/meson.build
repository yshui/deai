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
  'exec.lua',
  'proctitle.lua',
  'unittests.lua',
  'promise.lua',
  'async.lua',
  'cycle.lua',
  'twoway.lua',
  'promise_early_exit.lua',
  'spawn.lua',
  'next.lua',
]
foreach t : test_cases
  test(t, deai_exe, args: [
      'lua.load_script',
      's:' + (meson.current_source_dir() / t)
    ]
    , timeout: 3
    , depends: builtin_scripts
    , env: ['DEAI_EXTRA_PLUGINS='+all_plugins_files])
endforeach

core_test_cases = [
  'conversion_test.c',
  'anonymous_root_test.c',
  'drop_event_source_when_listener_is_attached.c',
  'c++_test.cc',
  'lua_fail_test.cc',
  'lua_return_test.cc',
]

foreach t : core_test_cases
  test_so = shared_library(t.underscorify(), t, c_args: base_c_args, cpp_args: base_cpp_args, name_prefix: '', include_directories: incs, link_with: [cpp_binding])
  test(t.underscorify()+'_test'
      , deai_exe, args: ['load_plugin', 's:' + test_so.full_path()]
      , depends: builtin_scripts
      , env: ['DEAI_EXTRA_PLUGINS='+all_plugins_files])
endforeach
