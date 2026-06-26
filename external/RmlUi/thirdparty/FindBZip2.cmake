# Custom FindBZip2 module for bundled bzip2
# Intercepts find_package(BZip2) calls

if(TARGET BZip2::BZip2)
    set(BZIP2_FOUND TRUE)
    set(BZIP2_LIBRARIES BZip2::BZip2)
    get_target_property(BZIP2_INCLUDE_DIR BZip2::BZip2 INTERFACE_INCLUDE_DIRECTORIES)
    if(BZIP2_INCLUDE_DIR)
        set(BZIP2_INCLUDE_DIRS "${BZIP2_INCLUDE_DIR}")
    endif()
    return()
endif()

# Fallback to standard find
include(FindPackageHandleStandardArgs)
find_path(BZIP2_INCLUDE_DIR bzlib.h)
find_library(BZIP2_LIBRARIES NAMES bz2 libbz2 bzip2)
find_package_handle_standard_args(BZip2
    REQUIRED_VARS BZIP2_LIBRARIES BZIP2_INCLUDE_DIR
)
if(BZIP2_FOUND AND NOT TARGET BZip2::BZip2)
    add_library(BZip2::BZip2 UNKNOWN IMPORTED)
    set_target_properties(BZip2::BZip2 PROPERTIES
        IMPORTED_LOCATION "${BZIP2_LIBRARIES}"
        INTERFACE_INCLUDE_DIRECTORIES "${BZIP2_INCLUDE_DIR}"
    )
endif()
mark_as_advanced(BZIP2_INCLUDE_DIR BZIP2_LIBRARIES)
