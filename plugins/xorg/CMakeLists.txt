pkg_check_modules(XCB xcb REQUIRED)
pkg_check_modules(XCB_XI xcb-xinput REQUIRED)

set(SRC
main.c
)

add_library(di_xorg SHARED ${SRC})
target_link_libraries(di_xorg ${XCB_LIBRARIES} ${XCB_XI_LIBRARIES})
target_include_directories(di_xorg PRIVATE ${XCB_INCLUDE_DIRS} ${XCB_XI_INCLUDE_DIRS})
target_compile_options(di_xorg PRIVATE ${DI_XORG_CFLAGS} ${DI_CFLAGS})

set_target_properties(di_xorg PROPERTIES PREFIX "")
