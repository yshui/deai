deps = [
  dependency('xcb', required: true),
  dependency('xcb-xinput', required: true),
  dependency('xcb-randr', required: true),
  dependency('xcb-xkb', required: true),
  dependency('xcb-keysyms', required: true),
  dependency('xkbcommon', required: true),
]
src = ['xorg.c', 'xinput.c', 'randr.c', 'key.c']
local_inc = include_directories('.')
di_xorg_lib = shared_library('di_xorg', src,
  include_directories: incs+[local_inc],
  dependencies: deps, c_args: base_c_args, name_prefix: '',
  install: true, install_dir: plugin_install_dir)
