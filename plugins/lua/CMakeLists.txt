pkg_check_modules(LUA lua>=5.3 REQUIRED)

set(SRC
lua.c
)

add_library(di_lua SHARED ${SRC})
target_link_libraries(di_lua ${LUA_LIBRARIES})
target_include_directories(di_lua PRIVATE ${LUA_INCLUDE_DIRS})
target_compile_options(di_lua PUBLIC ${LUA_CFLAGS_OTHER})
target_compile_options(di_lua PRIVATE ${DI_LUA_CFLAGS} ${DI_CFLAGS})

set_target_properties(di_lua PROPERTIES PREFIX "")

install(TARGETS di_lua
	LIBRARY DESTINATION ${DI_PLUGIN_INSTALL_DIR}
)
