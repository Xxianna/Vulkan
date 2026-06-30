# Custom Findlunasvg module for bundled lunasvg
# Intercepts find_package(lunasvg) calls

if(TARGET lunasvg::lunasvg)
    set(lunasvg_FOUND TRUE)
    return()
elseif(TARGET lunasvg)
    set(lunasvg_FOUND TRUE)
    if(NOT TARGET lunasvg::lunasvg)
        add_library(lunasvg::lunasvg ALIAS lunasvg)
    endif()
    return()
endif()

# Fallback to standard find
include(FindPackageHandleStandardArgs)
find_path(lunasvg_INCLUDE_DIR lunasvg.h PATH_SUFFIXES lunasvg)
find_library(lunasvg_LIBRARY NAMES lunasvg liblunasvg)
find_package_handle_standard_args(lunasvg
    REQUIRED_VARS lunasvg_LIBRARY lunasvg_INCLUDE_DIR
)
if(lunasvg_FOUND AND NOT TARGET lunasvg)
    add_library(lunasvg UNKNOWN IMPORTED)
    set_target_properties(lunasvg PROPERTIES
        IMPORTED_LOCATION "${lunasvg_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${lunasvg_INCLUDE_DIR}"
    )
    add_library(lunasvg::lunasvg ALIAS lunasvg)
endif()
mark_as_advanced(lunasvg_INCLUDE_DIR lunasvg_LIBRARY)
