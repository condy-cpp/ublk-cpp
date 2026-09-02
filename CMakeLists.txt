cmake_minimum_required(VERSION 3.13)

project(ublkcpp LANGUAGES CXX)

option(UBLKCPP_BUILD_TESTS "Build tests" OFF)
option(UBLKCPP_BUILD_UBLKCTL "Build ublkctl" OFF)
option(UBLKCPP_USE_URING_CMD128 "Use IORING_OP_URING_CMD128 for control commands" ON)
option(UBLKCPP_TESTS_STATIC_LINK "Use static linking for tests" OFF)

include(FetchContent)

set(DOCTEST_VERSION v2.5.0)
set(CLI11_VERSION v2.7.2)

FetchContent_Declare(
    doctest
    URL https://raw.githubusercontent.com/doctest/doctest/${DOCTEST_VERSION}/doctest/doctest.h
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_Declare(
    cli11
    URL https://github.com/CLIUtils/CLI11/releases/download/${CLI11_VERSION}/CLI11.hpp
    DOWNLOAD_NO_EXTRACT TRUE
)

add_library(ublkcpp INTERFACE)
target_include_directories(ublkcpp INTERFACE include)
target_compile_features(ublkcpp INTERFACE cxx_std_23) # TODO: cxx_std_26
if(UBLKCPP_USE_URING_CMD128)
    target_compile_definitions(ublkcpp INTERFACE UBLKCPP_USE_URING_CMD128)
endif()

set(CONDY_ENABLE_STDEXEC ON)
add_subdirectory(third_party/condy)
target_link_libraries(ublkcpp INTERFACE condy)

if(UBLKCPP_BUILD_TESTS)
    message(STATUS "Configuring tests in ${CMAKE_CURRENT_SOURCE_DIR}/tests")
    enable_testing()
    add_subdirectory(tests)
endif()

if(UBLKCPP_BUILD_UBLKCTL)
    message(STATUS "Configuring ublkctl in ${CMAKE_CURRENT_SOURCE_DIR}/bin")
    add_subdirectory(bin)
endif()
