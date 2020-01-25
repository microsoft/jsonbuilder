# Output target
#   uuid::uuid

include(AliasPkgConfigTarget)

# First try and find with PkgConfig
find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(UUID REQUIRED IMPORTED_TARGET uuid)
    if (TARGET PkgConfig::UUID)
        alias_pkg_config_target(uuid::uuid PkgConfig::UUID)
        return()
    endif ()
endif ()

# If that doesn't work, try again with old fashioned path lookup, with some caching
if (NOT (UUID_INCLUDE_DIR AND UUID_LIBRARY))
    find_path(UUID_INCLUDE_DIR
        NAMES uuid/uuid.h)
    find_library(UUID_LIBRARY
        NAMES uuid)

    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(UUID DEFAULT_MSG
        UUID_LIBRARY
        UUID_INCLUDE_DIR)

    mark_as_advanced(UUID_LIBRARY UUID_INCLUDE_DIR)
endif()

add_library(uuid::uuid UNKNOWN IMPORTED)
set_target_properties(uuid::uuid PROPERTIES
    IMPORTED_LOCATION "${UUID_LIBRARY}"
    IMPORTED_INCLUDE_DIRECTORIES "${UUID_INCLUDE_DIR}")
