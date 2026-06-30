# Thirdparty dependencies CMakeLists.txt
# This file adds bundled thirdparty dependencies as subdirectories

# Skip all install/export rules for thirdparty libraries
# This avoids "target not in any export set" errors
set(SKIP_INSTALL_ALL TRUE CACHE BOOL "Skip all install rules" FORCE)

# Make custom Find modules available to sub-dependencies (e.g. libpng finding ZLIB)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")

# ============================================================================
# ZLIB - Required by FreeType for compression support
# ============================================================================
if(NOT TARGET ZLIB::ZLIB)
    option(ZLIB_BUILD_TESTING "Build zlib tests" OFF)
    option(ZLIB_BUILD_SHARED "Build zlib shared" OFF)
    option(ZLIB_BUILD_STATIC "Build zlib static" ON)
    option(ZLIB_INSTALL "Enable zlib installation" OFF)

    add_subdirectory("zlib")

    # Ensure ZLIB::ZLIB target exists
    if(TARGET zlibstatic)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    elseif(TARGET zlib)
        add_library(ZLIB::ZLIB ALIAS zlib)
    endif()
endif()

# ============================================================================
# libpng - Required by FreeType for PNG support
# ============================================================================
if(NOT TARGET PNG::PNG)
    option(PNG_SHARED "Build libpng shared" OFF)
    option(PNG_STATIC "Build libpng static" ON)
    option(PNG_TESTS "Build libpng tests" OFF)
    option(PNG_TOOLS "Build libpng tools" OFF)
    option(PNG_INSTALL "Enable libpng installation" OFF)
    
    # For Emscripten, skip the pnglibconf generation step (requires zlib headers)
    # and use the prebuilt configuration instead
    if(EMSCRIPTEN)
        set(PNG_LIBCONF_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/libpng/pnglibconf.h.prebuilt" CACHE STRING "" FORCE)
    endif()

    # Pre-set ZLIB variables so libpng's find_package(ZLIB) succeeds
    set(ZLIB_FOUND TRUE CACHE BOOL "Zlib found")
    set(ZLIB_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/zlib" "${CMAKE_CURRENT_BINARY_DIR}/zlib")
    set(ZLIB_LIBRARIES "")

    # We need to let libpng know zlib is already built
    add_subdirectory("libpng")

    # Ensure PNG::PNG target exists and link to our zlib
    if(TARGET png_static)
        add_library(PNG::PNG ALIAS png_static)
    elseif(TARGET png)
        add_library(PNG::PNG ALIAS png)
    elseif(TARGET png16_static)
        add_library(PNG::PNG ALIAS png16_static)
    elseif(TARGET png16)
        add_library(PNG::PNG ALIAS png16)
    endif()
endif()

# ============================================================================
# BZip2 - Required by FreeType for BZip2 support
# Note: bzip2 does not have CMakeLists.txt, we create one inline
# ============================================================================
if(NOT TARGET BZip2::BZip2)
    if(NOT TARGET bz2)
        set(BZIP2_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bzip2")
        set(BZIP2_SOURCES
            ${BZIP2_SOURCE_DIR}/blocksort.c
            ${BZIP2_SOURCE_DIR}/bzlib.c
            ${BZIP2_SOURCE_DIR}/compress.c
            ${BZIP2_SOURCE_DIR}/crctable.c
            ${BZIP2_SOURCE_DIR}/decompress.c
            ${BZIP2_SOURCE_DIR}/huffman.c
            ${BZIP2_SOURCE_DIR}/randtable.c
        )
        
        # bzip2 requires bz_internal_error to be defined by the user
        set(BZ_INTERNAL_ERROR_SRC "${CMAKE_CURRENT_BINARY_DIR}/bz_internal_error.c")
        file(WRITE "${BZ_INTERNAL_ERROR_SRC}"
            "#include \"bzlib_private.h\"\n"
            "void bz_internal_error(int errcode) { (void)errcode; }\n"
        )
        
        add_library(bz2 STATIC ${BZIP2_SOURCES} "${BZ_INTERNAL_ERROR_SRC}")
        target_include_directories(bz2 PUBLIC ${BZIP2_SOURCE_DIR} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
        target_compile_definitions(bz2 PRIVATE -DBZ_NO_STDIO)
        set_target_properties(bz2 PROPERTIES
            OUTPUT_NAME bz2
            POSITION_INDEPENDENT_CODE ON
        )
        
        add_library(BZip2::BZip2 ALIAS bz2)
    endif()
endif()

# ============================================================================
# Brotli - Required by FreeType for WOFF2 support
# ============================================================================
if(NOT TARGET brotlicommon)
    option(BROTLI_BUNDLED_MODE "Bundled mode" OFF)
    option(BROTLI_DISABLE_TESTS "Disable tests" ON)
    option(BUILD_SHARED_LIBS "Build shared libs" OFF)

    add_subdirectory("brotli")
endif()

# ============================================================================
# FreeType - Font engine
# ============================================================================
if(NOT TARGET Freetype::Freetype)
    # FreeType should NOT depend on HarfBuzz to break the circular dependency.
    # We force this OFF so FreeType builds independently.
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "Disable HarfBuzz support in FreeType" FORCE)

    option(FT_DISABLE_ZLIB "Disable zlib support" OFF)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "Disable bzip2 support" FORCE)
    option(FT_DISABLE_PNG "Disable PNG support" OFF)
    option(FT_DISABLE_BROTLI "Disable Brotli support" OFF)

    # Build FreeType as static library by default
    if(NOT DEFINED BUILD_SHARED_LIBS OR NOT BUILD_SHARED_LIBS)
        set(FT_DISABLE_SHARED ON)
        set(FT_ENABLE_STATIC ON)
    endif()

    add_subdirectory("freetype")

    # Ensure Freetype::Freetype target exists for downstream consumers (e.g. RmlUi)
    if(NOT TARGET Freetype::Freetype)
        if(TARGET freetype)
            add_library(Freetype::Freetype ALIAS freetype)
        elseif(TARGET freetype-interface)
            add_library(Freetype::Freetype ALIAS freetype-interface)
        endif()
    endif()
endif()

# ============================================================================
# HarfBuzz - Text shaping library (MUST be after FreeType)
# ============================================================================
if(NOT TARGET harfbuzz::harfbuzz AND NOT TARGET harfbuzz)
    # HarfBuzz requires CMake 3.14+
    set(CMAKE_POLICY_VERSION_MINIMUM 3.14)

    # Enable FreeType support in HarfBuzz so it builds hb-ft module
    set(HB_HAVE_FREETYPE ON CACHE BOOL "Enable freetype interop helpers" FORCE)
    
    option(HB_BUILD_UTILS "Build harfbuzz utilities" OFF)
    option(HB_BUILD_SUBSET "Build harfbuzz-subset" OFF)
    option(HB_BUILD_RASTER "Build harfbuzz-raster" OFF)
    option(HB_BUILD_VECTOR "Build harfbuzz-vector" OFF)
    option(HB_BUILD_GPU "Build harfbuzz-gpu" OFF)
    option(HB_HAVE_CORETEXT "Enable CoreText shaper backend" OFF)
    option(HB_HAVE_GLIB "Enable GLib features" OFF)
    option(HB_HAVE_ICU "Enable ICU support" OFF)
    option(HB_HAVE_CAIRO "Enable cairo interop helpers" OFF)
    option(HB_HAVE_GRAPHITE2 "Enable Graphite2 shaper" OFF)
    option(HB_HAVE_UNISCRIBE "Enable Uniscribe shaper backend" OFF)
    option(HB_HAVE_GDI "Enable GDI integration helpers" OFF)
    option(HB_HAVE_DIRECTWRITE "Enable DirectWrite shaper backend" OFF)

    add_subdirectory("harfbuzz")

    if(TARGET harfbuzz AND NOT TARGET harfbuzz::harfbuzz)
        add_library(harfbuzz::harfbuzz ALIAS harfbuzz)
    endif()
    if(TARGET harfbuzz AND NOT TARGET HarfBuzz::HarfBuzz)
        add_library(HarfBuzz::HarfBuzz ALIAS harfbuzz)
    endif()
endif()

# ============================================================================
# GLFW - Window/Input management (only needed for GLFW backends)
# ============================================================================
if(RMLUI_BACKEND MATCHES "^(BackwardCompatible_)?GLFW" AND NOT TARGET glfw)
    option(GLFW_BUILD_DOCS "Build docs" OFF)
    option(GLFW_BUILD_EXAMPLES "Build examples" OFF)
    option(GLFW_BUILD_TESTS "Build tests" OFF)
    option(GLFW_INSTALL "Enable installation" OFF)
    
    # Disable platform-specific support not needed for Emscripten
    if(EMSCRIPTEN)
        set(GLFW_BUILD_WIN32 OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_COCOA OFF CACHE BOOL "" FORCE)
    endif()

    add_subdirectory("glfw")
endif()

# ============================================================================
# rlottie - Lottie animation support
# ============================================================================
if(NOT TARGET rlottie::rlottie AND NOT TARGET rlottie)
    option(LOTTIE_MODULE "Enable LOTTIE MODULE SUPPORT" OFF)
    option(LOTTIE_THREAD "Enable LOTTIE THREAD SUPPORT" OFF)
    option(LOTTIE_CACHE "Enable LOTTIE CACHE SUPPORT" ON)
    option(LOTTIE_TEST "Build LOTTIE AUTOTESTS" OFF)
    option(LOTTIE_CCACHE "Enable LOTTIE ccache SUPPORT" OFF)
    option(LOTTIE_ASAN "Compile with asan" OFF)
    option(BUILD_SHARED_LIBS "Build shared libs" OFF)

    # rlottie requires CMake >= 3.3 but newer CMake versions dropped support
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

    # Emscripten: patch rlottie CMakeLists.txt to skip --no-undefined and example dir
    set(_rlottie_cmake "${CMAKE_CURRENT_SOURCE_DIR}/rlottie/CMakeLists.txt")
    set(_rlottie_cmake_orig "${CMAKE_CURRENT_SOURCE_DIR}/rlottie/CMakeLists.txt.orig")
    set(_rlottie_example_cmake "${CMAKE_CURRENT_SOURCE_DIR}/rlottie/example/CMakeLists.txt")
    set(_rlottie_example_bak "${CMAKE_CURRENT_SOURCE_DIR}/rlottie/example/CMakeLists.txt.bak")
    if(EMSCRIPTEN)
        # Save original CMakeLists.txt
        if(NOT EXISTS "${_rlottie_cmake_orig}")
            configure_file("${_rlottie_cmake}" "${_rlottie_cmake_orig}" COPYONLY)
        endif()
        # Patch: skip --no-undefined for Emscripten
        file(READ "${_rlottie_cmake_orig}" _rlottie_tmp)
        string(REPLACE "elseif(NOT MSVC)" "elseif(NOT MSVC AND NOT EMSCRIPTEN)" _rlottie_tmp "${_rlottie_tmp}")
        file(WRITE "${_rlottie_cmake}" "${_rlottie_tmp}")
        unset(_rlottie_tmp)
        # Disable example directory
        if(EXISTS "${_rlottie_example_cmake}")
            file(RENAME "${_rlottie_example_cmake}" "${_rlottie_example_bak}")
            file(WRITE "${_rlottie_example_cmake}" "")
        endif()
    endif()

    add_subdirectory("rlottie")

    # Restore originals
    if(EMSCRIPTEN)
        if(EXISTS "${_rlottie_example_bak}")
            file(RENAME "${_rlottie_example_bak}" "${_rlottie_example_cmake}")
        endif()
        if(EXISTS "${_rlottie_cmake_orig}")
            file(RENAME "${_rlottie_cmake_orig}" "${_rlottie_cmake}")
        endif()
    endif()
endif()

# ============================================================================
# lunasvg - SVG rendering support
# ============================================================================
if(NOT TARGET lunasvg::lunasvg AND NOT TARGET lunasvg)
    option(USE_SYSTEM_PLUTOVG "Use system plutovg library" OFF)
    option(LUNASVG_BUILD_EXAMPLES "Build lunasvg examples" OFF)
    option(LUNASVG_STATIC "Build lunasvg as static library" ON)

    add_subdirectory("lunasvg")

    # Workaround: MSVC 18 crashes on plutovg with /O2 and PDB generation (CL!CloseTypeServerPDB bug)
    if(MSVC AND TARGET plutovg)
        # Disable PDB generation to prevent PDB server crash
        get_target_property(_plutovg_opts plutovg COMPILE_OPTIONS)
        if(_plutovg_opts AND NOT _plutovg_opts MATCHES "NOTFOUND")
            list(FILTER _plutovg_opts EXCLUDE REGEX "^/Z[7Ii]")
            set_target_properties(plutovg PROPERTIES COMPILE_OPTIONS "${_plutovg_opts}")
        endif()
        # Disable optimization to prevent optimizer crash
        target_compile_options(plutovg PRIVATE /Od)
    endif()
endif()
