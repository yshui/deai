pkg_check_modules(LUA lua>=5.3 REQUIRED)

set(SRC
main.c
)

add_library(piped_lua SHARED ${SRC})
target_link_libraries(piped_lua ${LUA_LIBRARIES})
target_include_directories(piped_lua PRIVATE ${LUA_INCLUDE_DIRS})
target_compile_options(piped_lua PUBLIC ${LUA_CFLAGS_OTHER})

set_target_properties(piped_lua PROPERTIES PREFIX "")
