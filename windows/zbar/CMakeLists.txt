cmake_minimum_required(VERSION 3.24)
project(zbar_windows VERSION 0.23.95 LANGUAGES C CXX)

if(NOT MSVC OR NOT WIN32)
    message(FATAL_ERROR "This integration requires native Windows MSVC.")
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(zbar_source
    URL https://codeload.github.com/mchehab/zbar/tar.gz/refs/tags/0.23.95
    URL_HASH SHA256=3bad3fe7b8764c6e2131cba91d40e22567a1365352914b62b55247b49124ed33
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(win_iconv_source
    URL https://codeload.github.com/win-iconv/win-iconv/tar.gz/refs/tags/v0.0.11
    URL_HASH SHA256=d42528544c8eb29d5711288efd8bd8838ac512f8942e37871203237597b5cb6d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# Upstream zbar uses Meson/MinGW. Compile its unmodified scanner sources
# with MSVC, without processor/UI bindings or optional external backends.
FetchContent_MakeAvailable(zbar_source)
FetchContent_GetProperties(win_iconv_source)
if(NOT win_iconv_source_POPULATED)
    FetchContent_Populate(win_iconv_source)
endif()

add_library(win_iconv STATIC "${win_iconv_source_SOURCE_DIR}/win_iconv.c")
target_include_directories(win_iconv PUBLIC "${win_iconv_source_SOURCE_DIR}")
target_compile_definitions(win_iconv PUBLIC WINICONV_CONST=)
target_compile_definitions(win_iconv PRIVATE _CRT_SECURE_NO_WARNINGS)
# Do not define USE_LIBICONV_DLL: conversion always uses the Windows API.

set(zbar "${zbar_source_SOURCE_DIR}/zbar")
configure_file(config.h.in config.h @ONLY)
add_library(zbar STATIC
    "${zbar}/config.c" "${zbar}/error.c" "${zbar}/symbol.c"
    "${zbar}/image.c" "${zbar}/convert.c" "${zbar}/refcnt.c"
    "${zbar}/video.c" "${zbar}/video/null.c" "${zbar}/misc.c"
    "${zbar}/img_scanner.c" "${zbar}/scanner.c" "${zbar}/decoder.c"
    "${zbar}/decoder/ean.c" "${zbar}/decoder/databar.c"
    "${zbar}/decoder/code128.c" "${zbar}/decoder/code93.c"
    "${zbar}/decoder/code39.c" "${zbar}/decoder/codabar.c"
    "${zbar}/decoder/i25.c" "${zbar}/decoder/qr_finder.c"
    "${zbar}/qrcode/qrdec.c" "${zbar}/qrcode/qrdectxt.c"
    "${zbar}/qrcode/rs.c" "${zbar}/qrcode/isaac.c"
    "${zbar}/qrcode/bch15_5.c" "${zbar}/qrcode/binarize.c"
    "${zbar}/qrcode/util.c")
target_include_directories(zbar
    PUBLIC "${zbar_source_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${zbar_source_SOURCE_DIR}" "${zbar}")
target_compile_definitions(zbar PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(zbar PUBLIC win_iconv winmm)

install(TARGETS zbar win_iconv ARCHIVE DESTINATION lib)
install(DIRECTORY "${zbar_source_SOURCE_DIR}/include/" DESTINATION include)
install(FILES "${zbar_source_SOURCE_DIR}/COPYING"
    "${zbar_source_SOURCE_DIR}/LICENSE.md" DESTINATION licenses/zbar)
install(FILES "${win_iconv_source_SOURCE_DIR}/readme.txt"
    DESTINATION licenses/win-iconv)

include(CTest)
if(BUILD_TESTING)
    add_executable(scanner-test ../../tests/windows/scanner-test.cpp)
    target_link_libraries(scanner-test PRIVATE zbar gdiplus)
    target_compile_features(scanner-test PRIVATE cxx_std_11)
    add_test(NAME scanner-integration COMMAND scanner-test
        "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Resources"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/windows/resources")
    set_tests_properties(scanner-integration PROPERTIES TIMEOUT 180)
endif()
