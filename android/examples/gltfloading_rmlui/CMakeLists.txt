cmake_minimum_required(VERSION 3.10.0 FATAL_ERROR)

set(NAME gltfloading_rmlui)

set(SRC_DIR ../../../examples/${NAME})
set(BASE_DIR ../../../base)
set(EXTERNAL_DIR ../../../external)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20 -DVK_USE_PLATFORM_ANDROID_KHR")

# ============================================================================
# RmlUi - build as static library with bundled dependencies
# ============================================================================
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)
set(RMLUI_SVG_PLUGIN ON CACHE BOOL "" FORCE)
set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
set(RMLUI_COMPILER_OPTIONS OFF CACHE BOOL "" FORCE)

# Build bundled dependencies (zlib, freetype, etc.) at external/ level
add_subdirectory(${EXTERNAL_DIR} ${CMAKE_BINARY_DIR}/external)
set(RMLUI_USING_BUNDLED_DEPENDENCIES TRUE)

add_subdirectory(${EXTERNAL_DIR}/RmlUi ${CMAKE_BINARY_DIR}/RmlUi)

# ============================================================================
# Main native library
# ============================================================================
file(GLOB EXAMPLE_SRC "${SRC_DIR}/*.cpp")

add_library(native-lib SHARED
    ${EXAMPLE_SRC}
    ${BASE_DIR}/RmlUiOverlay.cpp
    ${EXTERNAL_DIR}/RmlUi/Backends/RmlUi_Renderer_VK.cpp
)

add_library(native-app-glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

add_subdirectory(../base ${CMAKE_SOURCE_DIR}/../base)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")

include_directories(${BASE_DIR})
# Don't include ${EXTERNAL_DIR} directly - it contains desktop-only vulkan/vulkan.h
# Include only needed subdirectories instead
include_directories(${EXTERNAL_DIR}/glm)
include_directories(${EXTERNAL_DIR}/imgui)
include_directories(${EXTERNAL_DIR}/tinygltf)
include_directories(${EXTERNAL_DIR}/RmlUi/Include)
include_directories(${EXTERNAL_DIR}/RmlUi/Backends)
include_directories(${ANDROID_NDK}/sources/android/native_app_glue)

target_link_libraries(
    native-lib
    native-app-glue
    libbase
    rmlui
    rmlui_debugger
    vulkan
    android
    log
    z
)
