add_executable(ublk-nop nop.cpp)
target_link_libraries(ublk-nop PRIVATE ublkcpp)

add_executable(ublk-loop loop.cpp)
target_link_libraries(ublk-loop PRIVATE ublkcpp)
