src = ['dbus.c', 'yxml.c', 'sedes.c']
dbus = dependency('dbus-1', required: true)
di_dbus_lib = shared_library('di_dbus', src,
  include_directories: incs,
  dependencies: dbus, c_args: base_c_args,
  name_prefix: '', install: true,
  install_dir: plugin_install_dir)
# install(['TARGETS', 'di_dbus', 'LIBRARY', 'DESTINATION', di_plugin_install_dir])
