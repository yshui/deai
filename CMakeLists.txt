cmake_minimum_required(VERSION 3.5)

find_package(PkgConfig)
include(CheckCCompilerFlag)

macro(cflag_check_and_apply tgt flag)
	string(REPLACE "-" "_" TMP "${flag}")
	string(REPLACE "=" "_" TMP "${TMP}")
	check_c_compiler_flag(${flag} is_${TMP}_supported)
	if(is_${TMP}_supported)
		message(STATUS "Compiler flag ${flag} is supported, add to CFLAGS")
		list(APPEND ${tgt} ${flag})
		get_directory_property(TMP COMPILE_OPTIONS)
	else()
		foreach(param ${ARGN})
			if (param STREQUAL "REQUIRED")
				message(FATAL_ERROR "Compiler doesn't support ${flag}")
			endif()
		endforeach()
	endif()
endmacro()

cflag_check_and_apply(DI_CFLAGS -fvisibility=hidden)
cflag_check_and_apply(DI_CFLAGS -fno-strict-aliasing)
cflag_check_and_apply(DI_CFLAGS -fms-extensions REQUIRED)
cflag_check_and_apply(DI_CFLAGS -Wno-microsoft-anon-tag)
cflag_check_and_apply(DI_CFLAGS -Wall)


set(DI_PLUGIN_INSTALL_DIR "lib/deai" CACHE FILEPATH "Install path for plugins.")
configure_file(config.h.in config.h)

add_subdirectory(plugins)

set(SRC
	main.c
	plugin.c
	event.c
	log.c
	helper.c
)

add_executable(deai ${SRC})
#cflag_check_and_apply(DI_CFLAGS -fplan9-extensions)

target_include_directories(deai PUBLIC "include")
pkg_check_modules(LIBFFI libffi>=3.0 REQUIRED)
target_link_libraries(deai ${LIBFFI_LIBRARIES})
target_include_directories(deai PRIVATE ${LIBFFI_INCLUDE_DIRS})
target_compile_options(deai PUBLIC ${LIBFFI_CFLAGS_OTHER})
target_compile_options(deai PRIVATE ${DI_CFLAGS})

set_target_properties(deai PROPERTIES LINK_FLAGS "-rdynamic")
set_target_properties(deai PROPERTIES PUBLIC_HEADER "include/deai.h;include/object.h;include/event.h;include/log.h")

find_path(LIBEV_INCLUDE_DIR ev.h)
find_library(LIBEV_LIBRARY NAMES ev)

target_include_directories(deai PRIVATE ${LIBEV_INCLUDE_DIR})
target_link_libraries(deai ${LIBEV_LIBRARY})

target_link_libraries(deai dl)

install(TARGETS deai
	RUNTIME DESTINATION bin
	PUBLIC_HEADER DESTINATION "include/deai"
)
