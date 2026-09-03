cmake_minimum_required(VERSION 3.13)

# Version is defined once in the version header.
set(UBLKCPP_VERSION_HEADER include/ublk/version.hpp)
file(READ ${UBLKCPP_VERSION_HEADER} _ublkcpp_version_h)
string(REGEX MATCH "#define UBLKCPP_VERSION_MAJOR ([0-9]+)" _ublkcpp_ver_m "${_ublkcpp_version_h}")
set(UBLKCPP_VERSION_MAJOR ${CMAKE_MATCH_1})
string(REGEX MATCH "#define UBLKCPP_VERSION_MINOR ([0-9]+)" _ublkcpp_ver_m "${_ublkcpp_version_h}")
set(UBLKCPP_VERSION_MINOR ${CMAKE_MATCH_1})
set(UBLKCPP_VERSION_STRING "v${UBLKCPP_VERSION_MAJOR}.${UBLKCPP_VERSION_MINOR}")

project(ublkcpp VERSION ${UBLKCPP_VERSION_MAJOR}.${UBLKCPP_VERSION_MINOR} LANGUAGES CXX)
message(STATUS "Configuring ublkcpp ${UBLKCPP_VERSION_STRING}")

option(UBLKCPP_BUILD_TESTS "Build tests" OFF)
option(UBLKCPP_BUILD_UBLKCTL "Build ublkctl" OFF)
option(UBLKCPP_BUILD_EXAMPLES "Build examples" OFF)
option(UBLKCPP_BUILD_DOCS "Build Doxygen documentation" OFF)
option(UBLKCPP_USE_URING_CMD128 "Use IORING_OP_URING_CMD128 for control commands" ON)
option(UBLKCPP_TESTS_STATIC_LINK "Use static linking for tests" OFF)
option(UBLKCPP_TESTS_ASAN "Enable address and undefined behavior sanitizers for tests" OFF)
set(UBLKCPP_EXECUTION_BACKEND "stdexec" CACHE STRING
    "std::execution implementation used via Condy: stdexec (default) or beman")

include(FetchContent)

set(DOCTEST_VERSION v2.5.0)
set(CLI11_VERSION v2.7.2)
set(DOXYGEN_AWESOME_CSS_VERSION v2.4.1)

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
FetchContent_Declare(
    doxygen-awesome-css
    URL https://raw.githubusercontent.com/jothepro/doxygen-awesome-css/${DOXYGEN_AWESOME_CSS_VERSION}/doxygen-awesome.css
    DOWNLOAD_NO_EXTRACT TRUE
)

add_library(ublkcpp INTERFACE)
target_include_directories(ublkcpp INTERFACE include)
target_compile_features(ublkcpp INTERFACE cxx_std_23) # TODO: cxx_std_26
if(UBLKCPP_USE_URING_CMD128)
    target_compile_definitions(ublkcpp INTERFACE UBLKCPP_USE_URING_CMD128)
endif()

if(UBLKCPP_EXECUTION_BACKEND STREQUAL "beman")
    set(CONDY_ENABLE_STDEXEC OFF)
    set(CONDY_ENABLE_BEMAN ON)
elseif(UBLKCPP_EXECUTION_BACKEND STREQUAL "stdexec")
    set(CONDY_ENABLE_STDEXEC ON)
    set(CONDY_ENABLE_BEMAN OFF)
else()
    message(FATAL_ERROR
        "UBLKCPP_EXECUTION_BACKEND must be 'stdexec' or 'beman', got '"
        "${UBLKCPP_EXECUTION_BACKEND}'")
endif()
message(STATUS "ublkcpp: execution backend: ${UBLKCPP_EXECUTION_BACKEND}")
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

if(UBLKCPP_BUILD_EXAMPLES)
    message(STATUS "Configuring examples in ${CMAKE_CURRENT_SOURCE_DIR}/examples")
    add_subdirectory(examples)
endif()

if(UBLKCPP_BUILD_DOCS)
    message(STATUS "Configuring docs in ${CMAKE_CURRENT_SOURCE_DIR}/docs")
    add_subdirectory(docs)
endif()
