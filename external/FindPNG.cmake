# Custom FindPNG module for bundled libpng
# Intercepts find_package(PNG) calls

if(TARGET PNG::PNG)
    set(PNG_FOUND TRUE)
    set(PNG_LIBRARY PNG::PNG)
    set(PNG_LIBRARIES PNG::PNG)
    get_target_property(_PNG_REAL_TARGET PNG::PNG ALIASED_TARGET)
    if(_PNG_REAL_TARGET)
        get_target_property(PNG_INCLUDE_DIRS ${_PNG_REAL_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
    else()
        get_target_property(PNG_INCLUDE_DIRS PNG::PNG INTERFACE_INCLUDE_DIRECTORIES)
    endif()
    unset(_PNG_REAL_TARGET)
    return()
endif()

# Fallback to standard find
include(FindPackageHandleStandardArgs)
find_path(PNG_PNG_INCLUDE_DIR png.h)
find_library(PNG_LIBRARY NAMES png libpng png16 png16_static)
find_package_handle_standard_args(PNG
    REQUIRED_VARS PNG_LIBRARY PNG_PNG_INCLUDE_DIR
)
if(PNG_FOUND AND NOT TARGET PNG::PNG)
    add_library(PNG::PNG UNKNOWN IMPORTED)
    set_target_properties(PNG::PNG PROPERTIES
        IMPORTED_LOCATION "${PNG_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${PNG_PNG_INCLUDE_DIR}"
    )
endif()
mark_as_advanced(PNG_PNG_INCLUDE_DIR PNG_LIBRARY)
