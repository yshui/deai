src = 'lua.c'
lua = dependency('lua', version: '>=5.3', required: false)
if not lua.found()
  lua = dependency('lua5.3', required: true)
endif
di_lua_lib = shared_library('di_lua', src,
  dependencies: lua, include_directories: incs,
  c_args: base_c_args, name_prefix: '',
  install: true, install_dir: plugin_install_dir)
