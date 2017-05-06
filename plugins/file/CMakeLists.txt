set(SRC
file.c
)

add_library(di_file SHARED ${SRC})
target_compile_options(di_file PRIVATE ${DI_CFLAGS})

set_target_properties(di_file PROPERTIES PREFIX "")

install(TARGETS di_file
	LIBRARY DESTINATION ${DI_PLUGIN_INSTALL_DIR}
)
