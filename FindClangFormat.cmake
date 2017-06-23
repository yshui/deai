#
#.rst:
# FindClangFormat
# ---------------
#
# The module defines the following variables
#
# ``CLANG_FORMAT_EXECUTABLE``
#   Path to clang-format executable
# ``CLANG_FORMAT_FOUND``
#   True if the clang-format executable was found.
#
# Example usage:
#
# .. code-block:: cmake
#
#    find_package(ClangFormat)
#    if(CLANG_FORMAT_FOUND)
#      message("clang-format executable found: ${CLANG_FORMAT_EXECUTABLE}")
#    endif()

find_program(CLANG_FORMAT_EXECUTABLE
  NAMES clang-format clang-format-3.9 clang-format-3.8
  DOC "clang-format executable"
)
mark_as_advanced(CLANG_FORMAT_EXECUTABLE)

if(CLANG_FORMAT_EXECUTABLE)
  set(CLANG_FORMAT_FOUND true)
endif()
