# Output target
#   uuid::uuid

include(AliasPkgConfigTarget)

if (TARGET uuid::uuid)
    return()
endif()

# First try and find with PkgConfig
find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(uuid REQUIRED IMPORTED_TARGET uuid)
    if (TARGET PkgConfig::uuid)
        alias_pkg_config_target(uuid::uuid PkgConfig::uuid)
        return()
    endif ()
endif ()

# If that doesn't work, try again with old fashioned path lookup, with some caching
if (NOT (uuid_INCLUDE_DIR AND uuid_LIBRARY))
    find_path(uuid_INCLUDE_DIR
        NAMES uuid/uuid.h)
    find_library(uuid_LIBRARY
        NAMES uuid)

    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(uuid DEFAULT_MSG
        uuid_LIBRARY
        uuid_INCLUDE_DIR)

    mark_as_advanced(uuid_LIBRARY uuid_INCLUDE_DIR)
endif()

add_library(uuid::uuid UNKNOWN IMPORTED)
set_target_properties(uuid::uuid PROPERTIES
    IMPORTED_LOCATION "${uuid_LIBRARY}"
    IMPORTED_INCLUDE_DIRECTORIES "${uuid_INCLUDE_DIR}")

set(uuid_FOUND TRUE)
