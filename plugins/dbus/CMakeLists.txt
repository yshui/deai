set(SRC dbus.c yxml.c sedes.c)

pkg_check_modules(LIBDBUS "dbus-1" REQUIRED)

add_library(di_dbus SHARED ${SRC})
target_compile_options(di_dbus PRIVATE ${DI_CFLAGS})
target_include_directories(di_dbus PRIVATE ${LIBDBUS_INCLUDE_DIRS})
target_link_libraries(di_dbus ${LIBDBUS_LIBRARIES})

set_target_properties(di_dbus PROPERTIES PREFIX "")

install(TARGETS di_dbus LIBRARY DESTINATION ${DI_PLUGIN_INSTALL_DIR})
