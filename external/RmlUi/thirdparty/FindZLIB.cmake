# Custom FindZLIB module for bundled zlib
# Intercepts find_package(ZLIB) calls

if(TARGET ZLIB::ZLIB)
    set(ZLIB_FOUND TRUE)
    set(ZLIB_LIBRARY ZLIB::ZLIB)
    set(ZLIB_LIBRARIES ZLIB::ZLIB)
    # Get include dirs from the real target (not alias)
    get_target_property(_ZLIB_REAL_TARGET ZLIB::ZLIB ALIASED_TARGET)
    if(_ZLIB_REAL_TARGET)
        get_target_property(ZLIB_INCLUDE_DIRS ${_ZLIB_REAL_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
    else()
        get_target_property(ZLIB_INCLUDE_DIRS ZLIB::ZLIB INTERFACE_INCLUDE_DIRECTORIES)
    endif()
    unset(_ZLIB_REAL_TARGET)
    return()
endif()

# Fallback to standard find
include(FindPackageHandleStandardArgs)
find_path(ZLIB_INCLUDE_DIR zlib.h)
find_library(ZLIB_LIBRARY NAMES zlib zlib1 zlibstatic zdll z)
find_package_handle_standard_args(ZLIB
    REQUIRED_VARS ZLIB_LIBRARY ZLIB_INCLUDE_DIR
)
if(ZLIB_FOUND AND NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB UNKNOWN IMPORTED)
    set_target_properties(ZLIB::ZLIB PROPERTIES
        IMPORTED_LOCATION "${ZLIB_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ZLIB_INCLUDE_DIR}"
    )
endif()
mark_as_advanced(ZLIB_INCLUDE_DIR ZLIB_LIBRARY)
