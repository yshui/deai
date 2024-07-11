src = [ 'lua.c', 'compat.c' ]

lua_candidates = [ 'luajit', 'lua51', 'lua5.1', 'lua-5.1',
                   'lua52', 'lua5.2', 'lua-5.2',
                   'lua53', 'lua5.3', 'lua-5.3', 'lua' ]

if get_option('preferred_lua') != ''
  lua_candidates = [ get_option('preferred_lua') ]
endif

extra_c_args = []

foreach lua_candidate : lua_candidates
  lua = dependency(lua_candidate, required: false)
  if not lua.found()
    continue
  endif

  version = lua.version()
  # compat with luajit
  abiver = lua.get_pkgconfig_variable('abiver', default: '')
  if abiver != ''
    version = abiver
  endif

  if version.version_compare('<5.1')
    continue
  endif

  if version.version_compare('<5.2')
    extra_c_args += [ '-DNEED_LUAL_SETFUNCS', '-DNEED_LUAL_TOLSTRING' ]
  endif

  if version.version_compare('<5.3')
    extra_c_args += [ '-DNEED_LUA_ISINTEGER' ]
  endif

  break
endforeach

if not lua.found()
  error('No sutiable lua candidate found')
else
  message('Using lua package \''+lua.name()+'\', version: '+lua.version()+', '+
          'abiver: '+lua.get_pkgconfig_variable('abiver', default: ''))
endif

di_lua_lib = shared_library('di_lua', src,
  dependencies: lua, include_directories: incs,
  c_args: base_c_args + extra_c_args, name_prefix: '',
  install: true, install_dir: plugin_install_dir)

install_data('builtin.lua', install_dir:  plugin_install_dir / 'lua', install_tag: 'runtime')

# copy builtin.lua to builddir, so unittests can load it
configure_file(input: 'builtin.lua', output: 'builtin.lua', copy: true)
all_plugins += [ di_lua_lib ]
