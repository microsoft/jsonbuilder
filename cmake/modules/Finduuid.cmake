#
# Input variables:
#   UUID_PREFIX
#
# Output variables
#   UUID_FOUND
#   UUID_LIBRARIES
#   UUID_INCLUDE_DIRS
#

if (UUID_INCLUDE_DIRS AND UUID_LIBRARIES)
    set(UUID_FIND_QUIETLY TRUE)
else ()
    find_path(UUID_INCLUDE_DIRS NAMES uuid/uuid.h HINTS ${UUID_PREFIX})
    find_library(UUID_LIBRARIES NAMES uuid HINTS ${UUID_PREFIX})

    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(UUID DEFAULT_MSG UUID_LIBRARIES UUID_INCLUDE_DIRS)

    mark_as_advanced(UUID_LIBRARIES UUID_INCLUDE_DIRS)
endif()

add_library(uuid INTERFACE)
target_include_directories(uuid INTERFACE ${UUID_INCLUDE_DIRS})
target_link_libraries(uuid INTERFACE ${UUID_LIBRARIES})
