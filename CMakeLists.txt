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
cflag_check_and_apply(DI_CFLAGS -fms-extensions REQUIRED)
cflag_check_and_apply(DI_CFLAGS -Wno-microsoft-anon-tag)

add_subdirectory(plugins)

set(SRC
	main.c
	plugin.c
	event.c
	log.c
	script.c
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

find_path(LIBEV_INCLUDE_DIR ev.h)
find_library(LIBEV_LIBRARY NAMES ev)

target_include_directories(deai PRIVATE ${LIBEV_INCLUDE_DIR})
target_link_libraries(deai ${LIBEV_LIBRARY})

target_link_libraries(deai dl)
