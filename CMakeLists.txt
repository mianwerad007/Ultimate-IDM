cmake_minimum_required(VERSION 3.15)
project(UltimateDownloaderGUI CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "This project targets Windows only (Win32 + DirectX11).")
endif()

# ---- Dear ImGui (place the library here first, see README.md) ----
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/imgui)

add_executable(UltimateDownloaderGUI WIN32
    src/main.cpp
    src/DownloadEngine.cpp
    assets/app.rc
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(UltimateDownloaderGUI PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    src
    assets
)

# ---- libcurl ----
# Easiest path: vcpkg (recommended)
#   vcpkg install curl:x64-windows
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake
find_package(CURL REQUIRED)
target_link_libraries(UltimateDownloaderGUI PRIVATE CURL::libcurl)

# ---- Windows system libs ----
target_link_libraries(UltimateDownloaderGUI PRIVATE
    d3d11.lib
    d3dcompiler.lib
    dxgi.lib
    shell32.lib
    comdlg32.lib
)

if(MSVC)
    target_compile_options(UltimateDownloaderGUI PRIVATE /W3)
endif()

# ---- Native messaging host (tiny console helper, no ImGui/DX/curl needed) ----
# This is the program Chrome/Edge actually launches for browser integration.
add_executable(UltimateDownloaderHost src/NativeHost.cpp)
target_link_libraries(UltimateDownloaderHost PRIVATE shell32.lib)
if(MSVC)
    target_compile_options(UltimateDownloaderHost PRIVATE /W3)
endif()
