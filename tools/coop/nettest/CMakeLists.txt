# Test program for the co-op transport, driven by tools/coop/net_test.py. Not part of the default build.
# EXCLUDE_FROM_ALL goes here, not on add_subdirectory: Visual Studio leaves the targets of an excluded directory out of
# the solution, and cmake --build --target then cannot find them.
add_executable(coop_net_test EXCLUDE_FROM_ALL
    coop_net_test.c
    ${CMAKE_SOURCE_DIR}/src/port/coop/net/net_core.c
    ${CMAKE_SOURCE_DIR}/src/port/coop/net/transport_sdlnet.c
)
target_include_directories(coop_net_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(coop_net_test PRIVATE USE_NETWORKING)
# SDL2_net does not pass SDL2 on to whatever links it. A static SDL2 (vcpkg on Windows) only has the -static target.
find_package(SDL2 REQUIRED)
target_link_libraries(coop_net_test PRIVATE ${COOP_SDL2_NET}
    "$<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>")
set_target_properties(coop_net_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/coop-net-test
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
