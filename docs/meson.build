add_languages('cpp')
cpp = meson.get_compiler('cpp')

executable('docgen', ['docgen.cpp'], dependencies:
  [
    dependency('llvm'),
    cpp.find_library('libclangAST'),
    cpp.find_library('libclangFrontend'),
    cpp.find_library('libclangBasic'),
    cpp.find_library('libclangLex'),
    cpp.find_library('libclangEdit'),
    cpp.find_library('libclangTooling'),
    cpp.find_library('libclangSerialization'),
    cpp.find_library('libclangSema'),
    cpp.find_library('libclangAnalysis'),
    cpp.find_library('libclangDriver'),
    cpp.find_library('libclangParse')
  ], cpp_args: [ '-std=c++17', '-DBUILD_DIR="'+meson.current_build_dir()+'/.."' ], link_args: [ ]
)
