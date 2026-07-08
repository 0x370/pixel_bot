cmake_minimum_required(VERSION 3.28)
project(pixelbot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Frame pointers are required so std::stacktrace resolves to file:line.
add_compile_options(
    -O2 -g -fno-omit-frame-pointer
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

# --- std::stacktrace backend ---------------------------------------------------
# GCC >=14 ships std::stacktrace in -lstdc++exp; GCC 12-13 in -lstdc++_libbacktrace.
execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-search-dirs
    OUTPUT_VARIABLE _cxx_search_dirs OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REGEX REPLACE ".*libraries: *=([^ ]*).*" "\\1" _cxx_lib_dirs "${_cxx_search_dirs}")
string(REPLACE ":" ";" _cxx_lib_dirs "${_cxx_lib_dirs}")
find_library(STDCXX_EXP NAMES stdc++exp stdc++_libbacktrace HINTS ${_cxx_lib_dirs})
if(NOT STDCXX_EXP)
    message(FATAL_ERROR
        "std::stacktrace backend library not found. "
        "Install libstdc++exp (GCC >=14) or libstdc++_libbacktrace (GCC 12-13).")
endif()
mark_as_advanced(STDCXX_EXP)

# --- nlohmann/json (header-only) ----------------------------------------------
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# --- X11 (Xlib + MIT-SHM via xext) --------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11 xext)

enable_testing()

add_executable(pixelbot
    src/main.cpp
    src/config.cpp
    src/capture/x11_capture.cpp
    src/vision/primitives.cpp
    src/vision/color_detector.cpp
    src/vision/edge_detector.cpp
    src/vision/composite_detector.cpp
    src/vision/detector_factory.cpp
    src/aim/aim_controller.cpp
    src/input/uinput_mouse.cpp
    src/pipeline.cpp)

target_include_directories(pixelbot PRIVATE src)
target_link_libraries(pixelbot PRIVATE
    nlohmann_json::nlohmann_json
    PkgConfig::X11
    ${STDCXX_EXP})
target_compile_definitions(pixelbot PRIVATE
    $<$<CONFIG:Release>:NDEBUG>)

# --- Self-test (in-process synthetic frames, no X11/uinput needed) ------------
add_executable(pixelbot-selftest
    tests/self_test.cpp
    src/vision/primitives.cpp
    src/vision/color_detector.cpp
    src/vision/edge_detector.cpp
    src/vision/composite_detector.cpp
    src/config.cpp)
target_include_directories(pixelbot-selftest PRIVATE src)
target_link_libraries(pixelbot-selftest PRIVATE
    nlohmann_json::nlohmann_json
    ${STDCXX_EXP})
add_test(NAME pixelbot-selftest COMMAND pixelbot-selftest)
