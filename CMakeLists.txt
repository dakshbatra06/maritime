cmake_minimum_required(VERSION 3.20)
project(maritime VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# On macOS 26+, Apple moved libc++ headers into the SDK rather than the
# toolchain directory. The compiler still searches the old toolchain path,
# so we add the SDK's C++ include dir explicitly.
if(APPLE)
  execute_process(
    COMMAND xcrun --show-sdk-path
    OUTPUT_VARIABLE _sdk
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  set(_cxx_inc "${_sdk}/usr/include/c++/v1")
  if(EXISTS "${_cxx_inc}")
    message(STATUS "Adding SDK libc++ headers: ${_cxx_inc}")
    add_compile_options(-I${_cxx_inc})
  endif()
endif()

include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.18.5
)
FetchContent_MakeAvailable(nlohmann_json httplib)

add_library(engine
  src/engine/spatial_engine.cpp
  src/engine/anomaly.cpp
)
target_include_directories(engine PUBLIC src)
target_link_libraries(engine PUBLIC nlohmann_json::nlohmann_json)

add_executable(maritime_server src/main.cpp)
target_link_libraries(maritime_server PRIVATE engine httplib::httplib)

add_executable(ais_simulator tools/ais_simulator.cpp)
target_link_libraries(ais_simulator PRIVATE nlohmann_json::nlohmann_json httplib::httplib)

enable_testing()
add_executable(engine_tests tests/engine_test.cpp)
target_link_libraries(engine_tests PRIVATE engine)
add_test(NAME engine_tests COMMAND engine_tests)
