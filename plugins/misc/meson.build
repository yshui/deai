# Utils, collection of tools that don't fit anywhere else
misc = shared_module('misc', 'misc.cc', include_directories: incs, c_args: base_c_args
, cpp_args: base_cpp_args, name_prefix: '', link_with: [ libutils, cpp_binding ]
, install: true, install_dir: plugin_install_dir, gnu_symbol_visibility: 'hidden')
all_plugins += [ misc ]
