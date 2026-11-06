# Test program for the input sources, driven by tools/coop/input_test.py. Not part of the default build.
# EXCLUDE_FROM_ALL goes here, see tools/coop/nettest/CMakeLists.txt.
add_executable(coop_input_test EXCLUDE_FROM_ALL
    coop_input_test.c
    ${CMAKE_SOURCE_DIR}/src/port/coop/game/input.c
)
target_include_directories(coop_input_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/libultraship/include/libultraship
)
set_target_properties(coop_input_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/coop-input-test
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
