cmake_minimum_required(VERSION 3.7)

set(JB_SourceFiles)

set(PublicHeaderSrcPrefix ${PROJECT_SOURCE_DIR}/include/jsonbuilder)
set(JB_PublicHeaders ${PublicHeaderSrcPrefix}/JsonBuilder.h ${PublicHeaderSrcPrefix}/JsonRenderer.h)

# Add source to this project's executable.
add_library(jsonbuilder 
    JsonBuilder.cpp
    JsonRenderer.cpp
    PodVector.cpp)

target_include_directories(jsonbuilder PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(jsonbuilder PUBLIC uuid::uuid)
target_compile_features(jsonbuilder PUBLIC cxx_std_17)

set_property(TARGET jsonbuilder PROPERTY POSITION_INDEPENDENT_CODE ON)

install(TARGETS jsonbuilder DESTINATION lib)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
