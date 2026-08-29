message(STATUS "Fetching doctest")
FetchContent_MakeAvailable(doctest)

file(GLOB SRC CONFIGURE_DEPENDS "*.cpp")
add_executable(tests ${SRC})
target_link_libraries(tests PRIVATE ublkcpp)
target_include_directories(tests SYSTEM PRIVATE ${doctest_SOURCE_DIR})

add_test(NAME tests COMMAND tests)
