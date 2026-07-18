# CMake generated Testfile for 
# Source directory: /Users/dakshbatra/maritime
# Build directory: /Users/dakshbatra/maritime/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("engine_tests" "/Users/dakshbatra/maritime/build/engine_tests")
set_tests_properties("engine_tests" PROPERTIES  _BACKTRACE_TRIPLES "/Users/dakshbatra/maritime/CMakeLists.txt;54;add_test;/Users/dakshbatra/maritime/CMakeLists.txt;0;")
subdirs("_deps/nlohmann_json-build")
subdirs("_deps/httplib-build")
