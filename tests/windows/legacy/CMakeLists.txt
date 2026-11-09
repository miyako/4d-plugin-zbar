cmake_minimum_required(VERSION 3.24)
project(legacy_scanner_baseline LANGUAGES CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
add_executable(legacy-scanner ../scanner-test.cpp)
target_compile_features(legacy-scanner PRIVATE cxx_std_11)
target_compile_definitions(legacy-scanner PRIVATE LEGACY_ZBAR)
target_include_directories(legacy-scanner PRIVATE "${LEGACY_ROOT}/include/0.11")
target_link_libraries(legacy-scanner PRIVATE
    "${LEGACY_ROOT}/lib/Windows64/libzbar.0.11.lib" gdiplus winmm)
