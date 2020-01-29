get_filename_component(JSONBUILDER_CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)
include(CMakeFindDependencyMacro)

find_dependency(uuid REQUIRED)

if (NOT TARGET jsonbuilder::jsonbuilder)
    include("${JSONBUILDER_CMAKE_DIR}/jsonbuilderTargets.cmake")
endif ()

