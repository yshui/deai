project(git-clang-format-cmake)
cmake_minimum_required(VERSION 2.8)

set(GCFC_GIT_PATH "NOTSET"
   	CACHE STRING "Absolute path to the git executable")
set(GCFC_PYTHON_PATH "NOTSET"
   	CACHE STRING "Absolute path to the python executable")
set(GCFC_CLANGFORMAT_PATH "NOTSET"
   	CACHE STRING "Absolute path to the clang-format executable")

if("${GCFC_GIT_PATH}" STREQUAL "NOTSET")
	find_program(FIND_GIT git)
	if("${FIND_GIT}" STREQUAL "FIND_GIT-NOTFOUND")
		message(FATAL_ERROR
		   	"Could not find 'git' please set GCFC_GIT_PATH:STRING")
	else()
		set(GCFC_GIT_PATH ${FIND_GIT})
		message(STATUS "Found: ${GCFC_GIT_PATH}")
	endif()
else()
	if(NOT EXISTS ${GCFC_GIT_PATH})
		message(WARNING "Could not find git: ${GCFC_GIT_PATH}")
	else()
		message(STATUS "Found: ${GCFC_GIT_PATH}")
	endif()
endif()

if("${GCFC_PYTHON_PATH}" STREQUAL "NOTSET")
	find_program(FIND_PYTHON python)
	if("${FIND_PYTHON}" STREQUAL "FIND_PYTHON-NOTFOUND")
		message(FATAL_ERROR
		   	"Could not find 'python' please set GCFC_PYTHON_PATH:STRING")
	else()
		set(GCFC_PYTHON_PATH ${FIND_PYTHON})
		message(STATUS "Found: ${GCFC_PYTHON_PATH}")
	endif()
else()
	if(NOT EXISTS ${GCFC_PYTHON_PATH})
		message(WARNING "Could not find python: ${GCFC_PYTHON_PATH}")
	else()
		message(STATUS "Found: ${GCFC_PYTHON_PATH}")
	endif()
endif()

if("${GCFC_CLANGFORMAT_PATH}" STREQUAL "NOTSET")
	find_program(FIND_CLANGFORMAT clang-format)
	if("${FIND_CLANGFORMAT}" STREQUAL "FIND_CLANGFORMAT-NOTFOUND")
		message(FATAL_ERROR
		   	"Could not find 'python' please set GCFC_CLANGFORMAT_PATH:STRING")
	else()
		set(GCFC_CLANGFORMAT_PATH ${FIND_CLANGFORMAT})
		message(STATUS "Found: ${GCFC_CLANGFORMAT_PATH}")
	endif()
else()
	if(NOT EXISTS ${GCFC_CLANGFORMAT_PATH})
		message(WARNING "Could not find clang-format: ${GCFC_CLANGFORMAT_PATH}")
	else()
		message(STATUS "Found: ${GCFC_CLANGFORMAT_PATH}")
	endif()
endif()

set(GCFC_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/git-cmake-format.py)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/git-pre-commit-hook
	${CMAKE_SOURCE_DIR}/.git/hooks/pre-commit)

add_custom_target(format
	${GCFC_SCRIPT} --cmake ${GCFC_GIT_PATH} ${GCFC_CLANGFORMAT_PATH}
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
