cmake_minimum_required(VERSION 3.15)

project(jsonbuilder VERSION 0.2)

if(WIN32)
    add_compile_options(/W4 /WX /permissive-)
else()
    add_compile_options(
        -Wall
        -Wextra
        -Wformat
        -Wformat-security
        -Werror=format-security
        -Wstack-protector
        -Werror=stack-protector)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-D_FORTIFY_SOURCE=2)
    endif()
endif()

add_subdirectory(src)

# Only include testing stuff if we are the top level
if (${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME})
    enable_testing()

    list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/modules)
    if (EXISTS ${PROJECT_SOURCE_DIR}/external/Catch2/CMakeLists.txt)
        list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/external/Catch2/contrib")
        add_subdirectory(external/Catch2)
    else ()
        find_package(Catch2 REQUIRED)
    endif ()

    add_subdirectory(test)
endif ()
