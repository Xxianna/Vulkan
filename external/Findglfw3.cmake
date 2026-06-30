# Custom Findglfw3 module for bundled glfw
# This file intercepts find_package(glfw3) calls and uses our pre-built glfw target

if(TARGET glfw)
    set(glfw3_FOUND TRUE)
    set(glfw3_DIR "")
    if(NOT TARGET glfw3)
        add_library(glfw3 ALIAS glfw)
    endif()
    return()
endif()

include(FindPackageHandleStandardArgs)
find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h)
find_library(GLFW_LIBRARY NAMES glfw3 glfw)
find_package_handle_standard_args(glfw3
    REQUIRED_VARS GLFW_LIBRARY GLFW_INCLUDE_DIR
)
if(glfw3_FOUND AND NOT TARGET glfw)
    add_library(glfw UNKNOWN IMPORTED)
    set_target_properties(glfw PROPERTIES
        IMPORTED_LOCATION "${GLFW_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${GLFW_INCLUDE_DIR}"
    )
endif()
mark_as_advanced(GLFW_INCLUDE_DIR GLFW_LIBRARY)
