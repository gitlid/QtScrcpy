cmake_minimum_required(VERSION 3.19 FATAL_ERROR)
project(all)

include(CTest)
add_subdirectory(QtScrcpy)
if(BUILD_TESTING AND QSC_BUILD_ACTION_MACRO_TESTS)
    add_subdirectory(tests)
endif()
