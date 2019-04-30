cmake_minimum_required(VERSION 3.12)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/modules)
find_package(uuid REQUIRED)

set(JB_PrivateHeaders)
set(JB_SourceFiles JsonBuilder.cpp JsonRenderer.cpp PodVector.cpp)

set(PublicHeaderSrcPrefix ${PROJECT_SOURCE_DIR}/include/jsonbuilder)
set(JB_PublicHeaders ${PublicHeaderSrcPrefix}/JsonBuilder.h ${PublicHeaderSrcPrefix}/JsonRenderer.h)

# Add source to this project's executable.
add_library(jsonbuilder ${JB_SourceFiles} ${JB_PrivateHeaders} ${JB_PublicHeaders})
target_include_directories(jsonbuilder PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(jsonbuilder PUBLIC uuid)

set_property(TARGET jsonbuilder PROPERTY CXX_STANDARD 17)
set_property(TARGET jsonbuilder PROPERTY POSITION_INDEPENDENT_CODE ON)

install(TARGETS jsonbuilder DESTINATION lib)
install(FILES ${JB_PublicHeaders} DESTINATION include/jsonbuilder)
