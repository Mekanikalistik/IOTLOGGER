# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

# Exclude main.cpp to avoid C++ warnings and focus on main.c
list(FILTER app_sources EXCLUDE REGEX ".*main\.cpp$")

idf_component_register(SRCS ${app_sources}
                    REQUIRES mdns)
