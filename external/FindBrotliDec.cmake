# Custom FindBrotliDec module for bundled brotli
# Intercepts find_package(BrotliDec) calls

if(TARGET brotlicommon)
    set(BROTLIDEC_FOUND TRUE)
    set(BROTLIDEC_LIBRARIES brotlicommon brotlidec)
    get_target_property(BROTLIDEC_INCLUDE_DIRS brotlicommon INTERFACE_INCLUDE_DIRECTORIES)
    return()
endif()

# Fallback to standard find
include(FindPackageHandleStandardArgs)
find_path(BROTLIDEC_INCLUDE_DIRS brotli/decode.h)
find_library(BROTLIDEC_LIBRARIES NAMES brotlidec libbrotlidec)
find_package_handle_standard_args(BrotliDec
    REQUIRED_VARS BROTLIDEC_LIBRARIES BROTLIDEC_INCLUDE_DIRS
)
if(BROTLIDEC_FOUND AND NOT TARGET brotlidec)
    add_library(brotlidec UNKNOWN IMPORTED)
    set_target_properties(brotlidec PROPERTIES
        IMPORTED_LOCATION "${BROTLIDEC_LIBRARIES}"
        INTERFACE_INCLUDE_DIRECTORIES "${BROTLIDEC_INCLUDE_DIRS}"
    )
endif()
mark_as_advanced(BROTLIDEC_INCLUDE_DIRS BROTLIDEC_LIBRARIES)
