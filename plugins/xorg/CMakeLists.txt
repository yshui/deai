pkg_check_modules(XCB xcb REQUIRED)
pkg_check_modules(XCB_XI xcb-xinput REQUIRED)
pkg_check_modules(XCB_RR xcb-randr REQUIRED)
pkg_check_modules(XCB_XKB xcb-xkb REQUIRED)
pkg_check_modules(XCB_KEYSYMS xcb-keysyms REQUIRED)
pkg_check_modules(XKBCOMMON xkbcommon REQUIRED)

set(SRC
xorg.c
xinput.c
randr.c
key.c
)

add_library(di_xorg SHARED ${SRC})
target_link_libraries(di_xorg
	${XCB_LIBRARIES} ${XCB_XI_LIBRARIES} ${XCB_KEYSYMS_LIBRARIES}
	${XCB_RR_LIBRARIES} ${XCB_XKB_LIBRARIES} ${XKBCOMMON_LIBRARIES})
target_include_directories(di_xorg PRIVATE
	${XCB_INCLUDE_DIRS} ${XCB_XI_INCLUDE_DIRS} ${XCB_KEYSYMS_INCLUDE_DIRS}
	${XCB_RR_INCLUDE_DIRS} ${XCB_XKB_INCLUDE_DIRS} ${XKBCOMMON_INCLUDE_DIRS})
target_compile_options(di_xorg PRIVATE ${DI_XORG_CFLAGS} ${DI_CFLAGS})

set_target_properties(di_xorg PROPERTIES PREFIX "")

install(TARGETS di_xorg
	LIBRARY DESTINATION ${DI_PLUGIN_INSTALL_DIR}
)
