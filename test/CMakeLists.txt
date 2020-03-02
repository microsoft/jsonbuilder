cmake_minimum_required(VERSION 3.7)

# Add source to this project's executable.
add_executable(jsonbuilderTest CatchMain.cpp TestBuilder.cpp TestRenderer.cpp)
target_compile_features(jsonbuilderTest PRIVATE cxx_std_17)
target_link_libraries(jsonbuilderTest PRIVATE jsonbuilder Catch2::Catch2)

include(CTest)
include(Catch)
catch_discover_tests(jsonbuilderTest)
