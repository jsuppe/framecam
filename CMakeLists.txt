cmake_minimum_required(VERSION 3.20)
project(framecam LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    # vcam_linux.cpp (v4l2loopback) is the next backend; it needs framelink's
    # dma-buf backend first.
    message(FATAL_ERROR "framecam currently has a Windows backend only")
endif()

add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
include(FetchContent)

# --- framelink ----------------------------------------------------------------
# The camera OWNS a framelink channel; producers attach and render into the
# buffers it hands out. Sibling checkout by default.
set(FRAMELINK_DIR "${CMAKE_SOURCE_DIR}/../framelink" CACHE PATH
    "Path to the framelink checkout")
if(NOT EXISTS "${FRAMELINK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "framelink not found at '${FRAMELINK_DIR}'. Clone it beside framecam "
        "or pass -DFRAMELINK_DIR=<path>.")
endif()
set(FRAMELINK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${FRAMELINK_DIR}" "${CMAKE_BINARY_DIR}/framelink")

# --- softcam (vendored, MIT) --------------------------------------------------
# DirectShow filter + sender API in one DLL. Register once, elevated:
#   regsvr32 softcam.dll
set(SC ${CMAKE_SOURCE_DIR}/third_party/softcam/src)
file(GLOB SC_BASECLASSES ${SC}/baseclasses/*.cpp)
add_library(softcam SHARED
    ${SC_BASECLASSES}
    ${SC}/softcamcore/DShowSoftcam.cpp ${SC}/softcamcore/FrameBuffer.cpp
    ${SC}/softcamcore/Misc.cpp ${SC}/softcamcore/SenderAPI.cpp
    ${SC}/softcamcore/Watchdog.cpp
    ${SC}/softcam/softcam.cpp ${SC}/softcam/softcam.def)
target_include_directories(softcam PRIVATE ${SC}/baseclasses ${SC})
target_compile_definitions(softcam PRIVATE UNICODE _UNICODE)
# The 2004-era DirectShow baseclasses predate C++ conformance mode: they assign
# string literals to non-const pointers and rely on other permissive behaviour.
target_compile_options(softcam PRIVATE /EHsc /w /permissive /Zc:strictStrings-)
target_link_libraries(softcam PRIVATE
    strmiids winmm ole32 oleaut32 uuid advapi32 user32 gdi32)

# --- Media Foundation camera source (vendored VCamSample, MIT) ----------------
# Loaded by the OS Camera Frame Server, so the camera is visible to MF/UWP apps
# *and* bridged into DirectShow. Register once, elevated:
#   regsvr32 vkrender_vcam_source.dll
#
# The DLL NAME, its CLSID and the shared-memory names in src/vcam_shared.h are
# deliberately unchanged from when this lived in vkrender. They are baked into
# an already-registered COM server; renaming any of them silently breaks every
# installed camera until someone re-registers with elevation.
FetchContent_Declare(wil
    GIT_REPOSITORY https://github.com/microsoft/wil
    GIT_TAG v1.0.240803.1
    GIT_SHALLOW TRUE)
set(WIL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WIL_BUILD_PACKAGING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(wil)

set(VCS ${CMAKE_SOURCE_DIR}/third_party/VCamSample/VCamSampleSource)
add_library(vkrender_vcam_source SHARED
    ${VCS}/Activator.cpp ${VCS}/EnumNames.cpp ${VCS}/FrameGenerator.cpp
    ${VCS}/MFTools.cpp ${VCS}/MediaSource.cpp ${VCS}/MediaStream.cpp
    ${VCS}/Tools.cpp ${VCS}/WinTrace.cpp ${VCS}/dllmain.cpp ${VCS}/pch.cpp
    ${VCS}/VCamSampleSource.def)
target_include_directories(vkrender_vcam_source PRIVATE
    ${VCS} ${wil_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(vkrender_vcam_source PRIVATE UNICODE _UNICODE)
target_compile_options(vkrender_vcam_source PRIVATE /EHsc /w /await:strict)
target_link_libraries(vkrender_vcam_source PRIVATE
    mfplat mfuuid mfsensorgroup d2d1 dwrite windowscodecs
    runtimeobject ole32 oleaut32 advapi32 user32 shlwapi propsys)

# --- the camera itself --------------------------------------------------------
add_executable(framecam src/framecam_win.cpp)
target_include_directories(framecam PRIVATE ${SC}/softcam ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(framecam PRIVATE framelink softcam d3d11 dxgi
    mfplat mfuuid mfsensorgroup)
target_compile_options(framecam PRIVATE /EHsc)
