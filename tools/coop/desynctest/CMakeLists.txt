# Test program for the desync check, driven by tools/coop/lockstep_test.py (scenario desync_sim). Not part of the
# default build. EXCLUDE_FROM_ALL goes here, see tools/coop/nettest/CMakeLists.txt.
add_executable(coop_desync_test EXCLUDE_FROM_ALL
    coop_desync_test.c
    ${CMAKE_SOURCE_DIR}/src/port/coop/sync/desync.c
)
target_include_directories(coop_desync_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/libultraship/include/libultraship
)
set_target_properties(coop_desync_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/coop-desync-test
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
