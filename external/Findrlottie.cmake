# Custom Findrlottie module for bundled rlottie
# Intercepts find_package(rlottie) calls

if(TARGET rlottie::rlottie)
    set(rlottie_FOUND TRUE)
    return()
elseif(TARGET rlottie)
    set(rlottie_FOUND TRUE)
    if(NOT TARGET rlottie::rlottie)
        add_library(rlottie::rlottie ALIAS rlottie)
    endif()
    return()
endif()

# Fallback to standard find
include(FindPackageHandleStandardArgs)
find_path(rlottie_INCLUDE_DIR rlottie.h PATH_SUFFIXES rlottie)
find_library(rlottie_LIBRARY NAMES rlottie librlottie)
find_package_handle_standard_args(rlottie
    REQUIRED_VARS rlottie_LIBRARY rlottie_INCLUDE_DIR
)
if(rlottie_FOUND AND NOT TARGET rlottie)
    add_library(rlottie UNKNOWN IMPORTED)
    set_target_properties(rlottie PROPERTIES
        IMPORTED_LOCATION "${rlottie_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${rlottie_INCLUDE_DIR}"
    )
    add_library(rlottie::rlottie ALIAS rlottie)
endif()
mark_as_advanced(rlottie_INCLUDE_DIR rlottie_LIBRARY)
