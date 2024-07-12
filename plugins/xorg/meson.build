deps = [
  dependency('xcb', required: true),
  dependency('xcb-xinput', required: true),
  dependency('xcb-randr', required: true),
  dependency('xcb-xkb', required: true),
  dependency('xcb-keysyms', required: true),
  dependency('xkbcommon', required: true),
  cc.find_library('m', required: true),
]
src = ['xorg.c', 'xinput.c', 'randr.c', 'key.c']
local_inc = include_directories('.')
di_xorg_lib = shared_library('di_xorg', src,
  include_directories: incs+[local_inc],
  dependencies: deps, c_args: base_c_args, name_prefix: '',
  link_with: [ libutils ], install: true, install_dir: plugin_install_dir)

install_data('builtins.lua', install_dir:  plugin_install_dir / 'xorg', install_tag: 'runtime')

all_plugins += [ di_xorg_lib ]
builtin_scripts += custom_target('builtins.lua', output: 'builtins.lua', input: 'builtins.lua', command: ['cp', '@INPUT@', '@OUTPUT@'])
