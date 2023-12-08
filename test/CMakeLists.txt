cmake_minimum_required(VERSION 3.15)

set(LIB_TARGET_UUID)
if(NOT WIN32)
    find_package(uuid REQUIRED)
    set(LIB_TARGET_UUID uuid)
endif()

add_executable(jsonbuilderTest CatchMain.cpp TestBuilder.cpp TestRenderer.cpp)
target_compile_features(jsonbuilderTest PRIVATE cxx_std_17)
target_link_libraries(jsonbuilderTest PRIVATE jsonbuilder Catch2::Catch2 ${LIB_TARGET_UUID})

include(CTest)
include(Catch)
catch_discover_tests(jsonbuilderTest)
