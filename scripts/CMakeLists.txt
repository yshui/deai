configure_file(deai-lua.in deai-lua @ONLY)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/deai-lua DESTINATION bin)
