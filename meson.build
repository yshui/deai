project('deai', 'c', default_options: ['b_lundef=false'])
cc = meson.get_compiler('c')
if not cc.has_argument('-fvisibility=hidden') or not cc.has_argument('-fms-extensions')
  error('Unsupported compiler')
endif
if not cc.has_header('ev.h')
  error('libev header not found')
endif
test_c_args = [
    '-fno-strict-aliasing',
    '-fplan9-extensions',
    '-Wno-microsoft-anon-tag',
    '-Wnullability-completeness',
    '-Werror=incompatible-pointer-types',
    '-Werror=int-conversion',
    '-Werror=nonnull',
    '-Werror=implicit',
    '-Werror=implicit-fallthrough',
    '-Wall'
]
base_c_args = []
foreach arg: test_c_args
  if cc.has_argument(arg)
    base_c_args = base_c_args + [ arg ]
  endif
endforeach
base_c_args = base_c_args + [ '-fvisibility=hidden', '-fms-extensions', '-D_GNU_SOURCE' ]
base_ld_args = [ '-rdynamic' ]
plugin_install_dir = get_option('libdir')+'/deai'
if host_machine.system() == 'linux'
  have_setproctitle = true
endif
libev = cc.find_library('ev', required: true)
libffi = dependency('libffi', version: '>=3.0', required: true)
dl = cc.find_library('dl', required: true)
subdir('include')
incs = [deai_inc, include_directories('.')]
conf = configuration_data()
conf.set('HAVE_SETPROCTITLE', have_setproctitle)
conf.set('plugin_install_dir', get_option('prefix')+'/'+plugin_install_dir)
configure_file(input: 'config.h.in', output: 'config.h', configuration: conf)
subdir('scripts')
subdir('plugins')

deai_exe = executable('deai', [
  'main.c',
  'object.c',
  'callable.c',
  'event.c',
  'log.c',
  'helper.c',
  'env.c',
  'spawn.c',
  'string_buf.c'
], c_args: base_c_args
, dependencies: [libev, libffi, dl]
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
