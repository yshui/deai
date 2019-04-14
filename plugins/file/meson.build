src = 'file.c'
file_dependency = []
if host_machine.system() == 'freebsd'
  file_dependency = [ dependency('libinotify', required: true) ]
endif
di_file_lib = shared_library('di_file', src, c_args: base_c_args, include_directories: incs, name_prefix: '',
  dependencies: file_dependency, install: true, install_dir: plugin_install_dir)
