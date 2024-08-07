project('deai', ['c', 'cpp'], default_options: ['b_lundef=false', 'cpp_std=c++20'])
cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')
if not cc.has_argument('-fvisibility=hidden') or not cc.has_argument('-fms-extensions')
  error('Unsupported compiler')
endif
if not cc.has_header('ev.h')
  error('libev header not found')
endif

required_common_args = [ '-fno-strict-aliasing' ]

foreach arg: required_common_args
  if not cc.has_argument(arg) or not cpp.has_argument(arg)
    error('Required option ' + arg + ' not supported by the compiler')
  endif
endforeach

test_common_args = [
    '-Wno-microsoft-anon-tag',
    '-Wnullability-completeness',
    '-Werror=switch-enum',
    '-Werror=nonnull',
    '-Werror=implicit-fallthrough',
    '-Wno-c2x-extensions',
    '-Wno-c23-extensions',
    '-Wall'
]

required_c_args = [
  '-fexceptions',
  '-fms-extensions',
]

foreach arg: required_c_args
  if not cc.has_argument(arg)
    error('Required option ' + arg + ' not supported by the C compiler')
  endif
endforeach

test_c_args = [
    '-Werror=incompatible-pointer-types',
    '-Werror=int-conversion',
    '-Werror=implicit',
]

if get_option('b_sanitize').contains('undefined')
  ubsan_args = [
    '-fsanitize=null',
    '-fsanitize=nonnull-attribute',
    '-fsanitize=nullability-arg',
    '-fsanitize=nullability-assign',
    '-fsanitize=nullability-return',
  ]
  test_c_args += ubsan_args
endif

base_c_args = required_common_args + required_c_args
foreach arg: test_common_args + test_c_args
  if cc.has_argument(arg)
    base_c_args = base_c_args + [ arg ]
  endif
endforeach

base_cpp_args = required_common_args
foreach arg: test_common_args
  if cpp.has_argument(arg)
    base_cpp_args = base_cpp_args + [ arg ]
  endif
endforeach

base_c_args = base_c_args + [ '-D_GNU_SOURCE' ]
if get_option('unittests')
  base_c_args = base_c_args + [ '-DUNITTESTS' ]
endif
plugin_install_dir = get_option('libdir') / 'deai'
resources_install_dir = get_option('datadir') / 'deai'
if host_machine.system() == 'linux'
  have_setproctitle = true
else
  have_setproctitle = false
endif
libev = cc.find_library('ev', required: true)
libffi = dependency('libffi', version: '>=3.0', required: true)
dl = cc.find_library('dl', required: true)
subdir('include')
incs = [deai_inc, include_directories('.')]
conf = configuration_data()
conf.set('HAVE_SETPROCTITLE', have_setproctitle)
conf.set('TRACK_OBJECTS', get_option('track_objects'))
conf.set('ENABLE_STACK_TRACE', get_option('stack_trace'))
conf.set('plugin_install_dir', get_option('prefix') / plugin_install_dir)
conf.set('resources_dir', get_option('prefix') / resources_install_dir)
configure_file(input: 'config.h.in', output: 'config.h', configuration: conf)
subdir('scripts')
subdir('bindings')

libutils = static_library('libutils', ['string_buf.c'], include_directories: incs, c_args: base_c_args, gnu_symbol_visibility: 'hidden')

# link deai against a cpp library, so clang++ is used to link the final binary. this is
# in order to fix undefined symbol problem with UBSan
all_plugins = []
builtin_scripts = []
subdir('plugins')


fs = import('fs')
ldoc = find_program('ldoc', required: false)
if ldoc.found()
  extra_doc_source = []
  foreach script: builtin_scripts
    name = fs.relative_to(script.full_path(), meson.current_build_dir()).underscorify()
    extra_doc_source += custom_target(name+'.c'
      , output: name+'.c'
      , input: [script, files('build-support/ldoc2c.lua')]
      , command: [ldoc, '--filter', 'ldoc2c.dump', '@INPUT0@']
      , capture: true
      , build_always: true
      , env: ['LUA_PATH='+meson.current_source_dir()+'/build-support/?.lua;;'])
  endforeach
  # This is a dummy target to inject generated doc sources into the compile_commands.json
  # database, so our doc scanner can find them. This is never meant to be built.
  static_library('extra_doc_target', extra_doc_source, build_by_default: false)
endif


core_src = [
  'main.c'
, 'object.c'
, 'callable.c'
, 'event.c'
, 'log.c'
, 'os.c'
, 'spawn.c'
, 'exception.cc'
]
core_deps = [libev, libffi, dl]
if get_option('track_objects') or get_option('stack_trace')
  core_deps += [
    dependency('libunwind', required: true)
  , dependency('libunwind-generic', required: true)
  , dependency('libdw', required: true)
  ]
endif

if get_option('track_objects') or get_option('stack_trace')
  core_src += [ 'stacktrace.c' ]
endif

deai_exe = executable('deai', core_src, c_args: base_c_args
, cpp_args: base_cpp_args
, dependencies: core_deps
, link_with: [ cpp_binding, libutils ]
, include_directories: incs
, export_dynamic: true
, gnu_symbol_visibility: 'hidden'
, install: true)
all_plugins_files = []
foreach plugin: all_plugins
  all_plugins_files += [plugin.full_path()]
endforeach
all_plugins_files = ':'.join(all_plugins_files)
run_deai_data = configuration_data()
run_deai_data.set('all_plugins', all_plugins_files)
run_deai_data.set('deai_exe', deai_exe.full_path())
run_deai_data.set('resources_dir', meson.current_build_dir() / 'plugins')
configure_file(input: 'run_deai.sh.in', output: 'run_deai.sh', configuration: run_deai_data)
subdir('tests')

base_headers = [
  'include/deai/callable.h',
  'include/deai/compiler.h',
  'include/deai/deai.h',
  'include/deai/helper.h',
  'include/deai/object.h',
]
builtin_module_headers = [
  'include/deai/builtins/event.h',
  'include/deai/builtins/log.h',
  'include/deai/builtins/spawn.h'
]
cfg_data = configuration_data()
cfg_data.set('prefix', get_option('prefix'))
configure_file(configuration: cfg_data, input: 'desktop/deai.desktop.in', install: true, install_dir: 'share/xsessions', output: 'deai.desktop')
#install_data('desktop/deai.desktop', install_dir:'share/xsessions', install_mode: 'rw-r--r--')
install_headers(base_headers, subdir: 'deai')
install_headers(builtin_module_headers, subdir: 'deai/builtins')
