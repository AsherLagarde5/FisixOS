cmake_minimum_required(VERSION 3.20)
# Go render pipeline is built by build.bat before this step.
# See render_pipeline/ for Go sources and build.bat for the full build sequence.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Rust Safety Net Internal Pages Library
set(RUST_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/rust_internal_pages/target/release")
link_directories(${RUST_LIB_DIR})

# 100% From-Scratch Native Browser Engine (Zero Chromium / Zero WebView2)
add_library(browser_core STATIC browser_core.cpp)
target_compile_definitions(browser_core PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(browser_core PRIVATE /utf-8 /std:c++20)
target_link_libraries(browser_core PRIVATE winhttp user32 gdi32)

add_executable(browsybrowser WIN32 real_browser.cpp)
target_compile_definitions(browsybrowser PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(browsybrowser PRIVATE /utf-8 /std:c++20)
target_link_libraries(browsybrowser PRIVATE winhttp gdiplus user32 gdi32 ole32 shell32 dwmapi uxtheme shlwapi comctl32 rust_internal_pages.dll.lib)
add_custom_command(TARGET browsybrowser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${RUST_LIB_DIR}/rust_internal_pages.dll"
        "$<TARGET_FILE_DIR:browsybrowser>"
    COMMENT "Copy rust_internal_pages.dll next to browsybrowser.exe"
)

add_executable(test_engine test_engine.cpp)
target_compile_definitions(test_engine PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(test_engine PRIVATE /utf-8 /std:c++20)
target_link_libraries(test_engine PRIVATE winhttp gdiplus user32 gdi32 ole32 shell32 dwmapi uxtheme shlwapi comctl32 rust_internal_pages.dll.lib)
add_custom_command(TARGET test_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${RUST_LIB_DIR}/rust_internal_pages.dll"
        "$<TARGET_FILE_DIR:test_engine>"
    COMMENT "Copy rust_internal_pages.dll next to test_engine.exe"
)

add_executable(test_css test_css.cpp)
target_compile_definitions(test_css PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(test_css PRIVATE /utf-8 /std:c++20)

enable_testing()
add_test(NAME css_rendering_basics COMMAND test_css)
