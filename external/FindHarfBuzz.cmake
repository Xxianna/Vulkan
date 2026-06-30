# Custom FindHarfBuzz module for bundled harfbuzz
# Intercepts find_package(HarfBuzz) calls

if(TARGET harfbuzz::harfbuzz OR TARGET harfbuzz)
    set(HarfBuzz_FOUND TRUE PARENT_SCOPE)
    if(NOT TARGET harfbuzz::harfbuzz AND TARGET harfbuzz)
        add_library(harfbuzz::harfbuzz ALIAS harfbuzz)
    endif()
    return()
endif()

# Fallback to standard find
include(FindPackageHandleStandardArgs)
find_path(HarfBuzz_INCLUDE_DIR hb.h PATH_SUFFIXES harfbuzz)
find_library(HarfBuzz_LIBRARY NAMES harfbuzz libharfbuzz)
find_package_handle_standard_args(HarfBuzz
    REQUIRED_VARS HarfBuzz_LIBRARY HarfBuzz_INCLUDE_DIR
)
if(HarfBuzz_FOUND AND NOT TARGET harfbuzz)
    add_library(harfbuzz UNKNOWN IMPORTED)
    set_target_properties(harfbuzz PROPERTIES
        IMPORTED_LOCATION "${HarfBuzz_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${HarfBuzz_INCLUDE_DIR}"
    )
    add_library(harfbuzz::harfbuzz ALIAS harfbuzz)
    set(HarfBuzz_FOUND TRUE PARENT_SCOPE)
endif()
mark_as_advanced(HarfBuzz_INCLUDE_DIR HarfBuzz_LIBRARY)
