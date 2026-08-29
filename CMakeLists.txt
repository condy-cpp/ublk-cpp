cmake_minimum_required(VERSION 3.13)

project(ublkcpp LANGUAGES CXX)

option(UBLKCPP_BUILD_TESTS "Build tests" OFF)

include(FetchContent)

set(DOCTEST_VERSION v2.5.0)
FetchContent_Declare(
    doctest
    URL https://raw.githubusercontent.com/doctest/doctest/${DOCTEST_VERSION}/doctest/doctest.h
    DOWNLOAD_NO_EXTRACT TRUE
)

add_library(ublkcpp INTERFACE)
target_include_directories(ublkcpp INTERFACE include)
target_compile_features(ublkcpp INTERFACE cxx_std_23) # TODO: cxx_std_26

set(CONDY_ENABLE_STDEXEC ON)
add_subdirectory(third_party/condy)
target_link_libraries(ublkcpp INTERFACE condy)

if(UBLKCPP_BUILD_TESTS)
    message(STATUS "Configuring tests in ${CMAKE_CURRENT_SOURCE_DIR}/tests")
    enable_testing()
    add_subdirectory(tests)
endif()
