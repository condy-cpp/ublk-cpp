message(STATUS "Fetching CLI11")
FetchContent_MakeAvailable(cli11)

add_executable(ublkctl ublkctl.cpp)
target_link_libraries(ublkctl PRIVATE ublkcpp)
target_include_directories(ublkctl SYSTEM PRIVATE ${cli11_SOURCE_DIR})
