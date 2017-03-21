pkg_check_modules(LUA lua>=5.3 REQUIRED)

set(SRC
main.c
)

cflag_check_and_apply(DI_LUA_CFLAGS -fms-extensions REQUIRED)

add_library(di_lua SHARED ${SRC})
target_link_libraries(di_lua ${LUA_LIBRARIES})
target_include_directories(di_lua PRIVATE ${LUA_INCLUDE_DIRS})
target_compile_options(di_lua PUBLIC ${LUA_CFLAGS_OTHER})
target_compile_options(di_lua PRIVATE ${DI_LUA_CFLAGS})

set_target_properties(di_lua PROPERTIES PREFIX "")
