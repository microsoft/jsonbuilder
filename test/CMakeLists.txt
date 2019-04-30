cmake_minimum_required(VERSION 3.12)

# Add source to this project's executable.
add_executable(jsonbuilderTest CatchMain.cpp TestBuilder.cpp TestRenderer.cpp)
target_link_libraries(jsonbuilderTest PRIVATE jsonbuilder Catch2::Catch2)

set_property(TARGET jsonbuilderTest PROPERTY CXX_STANDARD 17)

include(CTest)
include(Catch)
catch_discover_tests(jsonbuilderTest)

install(TARGETS jsonbuilderTest DESTINATION bin)
