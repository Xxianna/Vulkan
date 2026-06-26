# Custom FindFreetype module for bundled freetype
# This file intercepts find_package(Freetype) calls and uses our pre-built freetype target

if(TARGET Freetype::Freetype)
    set(FREETYPE_FOUND TRUE)
    set(FREETYPE_LIBRARY Freetype::Freetype)
    set(FREETYPE_LIBRARIES Freetype::Freetype)
    get_target_property(FREETYPE_INCLUDE_DIRS Freetype::Freetype INTERFACE_INCLUDE_DIRECTORIES)
    return()
elseif(TARGET freetype)
    set(FREETYPE_FOUND TRUE)
    set(FREETYPE_LIBRARY freetype)
    set(FREETYPE_LIBRARIES freetype)
    get_target_property(FREETYPE_INCLUDE_DIRS freetype INTERFACE_INCLUDE_DIRECTORIES)
    
    if(NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
    return()
endif()

# Otherwise try normal find
include(FindPackageHandleStandardArgs)
find_path(FREETYPE_INCLUDE_DIR ft2build.h)
find_library(FREETYPE_LIBRARY NAMES freetype libfreetype)
find_package_handle_standard_args(Freetype
    REQUIRED_VARS FREETYPE_LIBRARY FREETYPE_INCLUDE_DIR
)
if(FREETYPE_FOUND AND NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype UNKNOWN IMPORTED)
    set_target_properties(Freetype::Freetype PROPERTIES
        IMPORTED_LOCATION "${FREETYPE_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FREETYPE_INCLUDE_DIR}"
    )
endif()
mark_as_advanced(FREETYPE_INCLUDE_DIR FREETYPE_LIBRARY)
