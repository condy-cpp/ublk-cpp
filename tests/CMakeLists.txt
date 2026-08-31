message(STATUS "Fetching doctest")
FetchContent_MakeAvailable(doctest)

file(GLOB SRC CONFIGURE_DEPENDS "*.cpp")
add_executable(tests ${SRC})
target_link_libraries(tests PRIVATE ublkcpp)
target_include_directories(tests SYSTEM PRIVATE ${doctest_SOURCE_DIR})

if (UBLKCPP_TESTS_STATIC_LINK)
    message(STATUS "Configuring tests with static linking")
    target_link_options(tests PRIVATE -static)
elseif (UBLKCPP_TESTS_ASAN)
    message(STATUS "Configuring tests with address and undefined behavior sanitizers")
    target_compile_options(tests PRIVATE -fsanitize=address,undefined)
    target_link_options(tests PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME tests COMMAND tests)
