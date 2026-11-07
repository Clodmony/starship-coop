# Test program for lockstep, driven by tools/coop/lockstep_test.py. Not part of the default build.
# EXCLUDE_FROM_ALL goes here, see tools/coop/nettest/CMakeLists.txt.
add_executable(coop_lockstep_test EXCLUDE_FROM_ALL
    coop_lockstep_test.c
    ${CMAKE_SOURCE_DIR}/src/port/coop/sync/lockstep.c
)
target_include_directories(coop_lockstep_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/libultraship/include/libultraship
)
set_target_properties(coop_lockstep_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/coop-lockstep-test
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
