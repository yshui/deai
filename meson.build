project('deai', ['c', 'cpp'], default_options: ['b_lundef=false', 'cpp_std=c++17'])
cc = meson.get_compiler('c')
if not cc.has_argument('-fvisibility=hidden') or not cc.has_argument('-fms-extensions')
  error('Unsupported compiler')
endif
if not cc.has_header('ev.h')
  error('libev header not found')
endif
test_common_args = [
    '-fno-strict-aliasing',
    '-fplan9-extensions',
    '-fms-extensions',
    '-Wno-microsoft-anon-tag',
    '-Wnullability-completeness',
    '-Werror=switch-enum',
    '-Werror=nonnull',
    '-Werror=implicit-fallthrough',
    '-Wall'
]

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

base_c_args = []
base_ld_args = [ '-rdynamic' ]
foreach arg: test_common_args + test_c_args
  if cc.has_argument(arg)
    base_c_args = base_c_args + [ arg ]
  endif
endforeach

base_cpp_args = []
cpp = meson.get_compiler('cpp')
foreach arg: test_common_args
  if cpp.has_argument(arg)
    base_cpp_args = base_cpp_args + [ arg ]
  endif
endforeach

base_c_args = base_c_args + [ '-fvisibility=hidden', '-D_GNU_SOURCE' ]
plugin_install_dir = get_option('libdir')+'/deai'
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
conf.set('plugin_install_dir', get_option('prefix')+'/'+plugin_install_dir)
configure_file(input: 'config.h.in', output: 'config.h', configuration: conf)
subdir('scripts')
subdir('bindings')

libutils = static_library('libutils', ['string_buf.c'], include_directories: incs)

# link deai against a cpp library, so clang++ is used to link the final binary. this is
# in order to fix undefined symbol problem with UBSan
cpp_dummy = static_library('cpp_dummy', ['dummy.cc'])

subdir('plugins')

deai_exe = executable('deai', [
  'main.c',
  'object.c',
  'callable.c',
  'event.c',
  'log.c',
  'helper.c',
  'os.c',
  'spawn.c',
  'string_buf.c',
], c_args: base_c_args
, dependencies: [libev, libffi, dl]
, link_with: [ cpp_dummy ]
, include_directories: incs
, link_args: base_ld_args
, install: true)
subdir('tests')

base_headers = [
  'include/deai/callable.h',
  'include/deai/compiler.h',
  'include/deai/deai.h',
  'include/deai/helper.h',
  'include/deai/object.h',
]
builtin_module_headers = [
  'include/deai/builtin/event.h',
  'include/deai/builtin/log.h',
  'include/deai/builtin/spawn.h'
]
install_data('desktop/deai.desktop', install_dir:'share/xsessions', install_mode: 'rw-r--r--')
install_headers(base_headers, subdir: 'deai')
install_headers(builtin_module_headers, subdir: 'deai/builtin')
