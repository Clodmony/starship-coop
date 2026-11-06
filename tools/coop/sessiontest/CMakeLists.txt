# Test program for the co-op session handshake, driven by tools/coop/session_test.py. Not part of the default build.
# EXCLUDE_FROM_ALL goes here, see tools/coop/nettest/CMakeLists.txt.
add_executable(coop_session_test EXCLUDE_FROM_ALL
    coop_session_test.c
    ${CMAKE_SOURCE_DIR}/src/port/coop/net/net_core.c
    ${CMAKE_SOURCE_DIR}/src/port/coop/net/transport_sdlnet.c
    ${CMAKE_SOURCE_DIR}/src/port/coop/session/identity.c
    ${CMAKE_SOURCE_DIR}/src/port/coop/session/session.c
    ${CMAKE_SOURCE_DIR}/src/port/coop/session/sim_cvars.c
)
target_include_directories(coop_session_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(coop_session_test PRIVATE USE_NETWORKING)
find_package(SDL2 REQUIRED)
target_link_libraries(coop_session_test PRIVATE ${COOP_SDL2_NET}
    "$<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>")
set_target_properties(coop_session_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/coop-session-test
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
