cmake_minimum_required(VERSION 3.15)

add_library(jsonbuilder 
    JsonBuilder.cpp
    JsonExceptions.cpp
    JsonRenderer.cpp
    PodVector.cpp)

target_include_directories(jsonbuilder 
    PUBLIC 
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_compile_features(jsonbuilder PUBLIC cxx_std_17)

set_property(TARGET jsonbuilder PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET jsonbuilder PROPERTY SOVERSION 0)

include(GNUInstallDirs)

install(TARGETS jsonbuilder 
    EXPORT jsonbuilder-export
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_LIBDIR})

add_library(jsonbuilder::jsonbuilder ALIAS jsonbuilder)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

set(JSONBUILDER_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/jsonbuilder)

install(EXPORT jsonbuilder-export
    FILE jsonbuilderTargets.cmake
    NAMESPACE jsonbuilder::
    DESTINATION ${JSONBUILDER_CMAKE_DIR})

install(FILES ${PROJECT_SOURCE_DIR}/cmake/modules/jsonbuilderConfig.cmake
    DESTINATION ${JSONBUILDER_CMAKE_DIR})
