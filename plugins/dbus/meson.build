src = ['dbus.c', 'sedes.c', 'signature.c' ]
dbus = dependency('dbus-1', required: true)
di_dbus_lib = shared_library('di_dbus', src,
  include_directories: incs,
  dependencies: dbus, c_args: base_c_args,
  name_prefix: '', install: true,
  link_with: [ libutils ],
  install_dir: plugin_install_dir)
# install(['TARGETS', 'di_dbus', 'LIBRARY', 'DESTINATION', di_plugin_install_dir])
all_plugins += [ di_dbus_lib ]
