cmake_minimum_required(VERSION 3.5)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")

find_package(PkgConfig)

add_subdirectory(plugins)

set(SRC
	main.c
	plugin.c
	event.c
)

add_executable(piped ${SRC})

target_include_directories(piped PUBLIC "include")

pkg_check_modules(LIBFFI libffi>=3.0 REQUIRED)
target_link_libraries(piped ${LIBFFI_LIBRARIES})
target_include_directories(piped PRIVATE ${LIBFFI_INCLUDE_DIRS})
target_compile_options(piped PUBLIC ${LIBFFI_CFLAGS_OTHER})

find_path(LIBEV_INCLUDE_DIR ev.h)
find_library(LIBEV_LIBRARY NAMES ev)

target_include_directories(piped PRIVATE ${LIBEV_INCLUDE_DIR})
target_link_libraries(piped ${LIBEV_LIBRARY})

target_link_libraries(piped dl)
