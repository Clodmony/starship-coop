# Test program for the co-op transport, driven by tools/coop/net_test.py. Not part of the default build.
# EXCLUDE_FROM_ALL goes here, not on add_subdirectory: Visual Studio leaves the targets of an excluded directory out of
# the solution, and cmake --build --target then cannot find them.
#
# coop_net_test_asan is the same program under AddressSanitizer and UBSan (net_test.py --asan). The flags are on this
# target alone, so it shares the normal build directory. MSVC has no UBSan, so there it does not exist.
set(COOP_NET_TEST_TARGETS coop_net_test)
if(NOT MSVC)
    list(APPEND COOP_NET_TEST_TARGETS coop_net_test_asan)
endif()
# SDL2_net does not pass SDL2 on to whatever links it. A static SDL2 (vcpkg on Windows) only has the -static target.
find_package(SDL2 REQUIRED)
foreach(COOP_TARGET ${COOP_NET_TEST_TARGETS})
    add_executable(${COOP_TARGET} EXCLUDE_FROM_ALL
        coop_net_test.c
        ${CMAKE_SOURCE_DIR}/src/port/coop/net/net_core.c
        ${CMAKE_SOURCE_DIR}/src/port/coop/net/transport_sdlnet.c
    )
    target_include_directories(${COOP_TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(${COOP_TARGET} PRIVATE USE_NETWORKING)
    target_link_libraries(${COOP_TARGET} PRIVATE ${COOP_SDL2_NET}
        "$<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>")
    set_target_properties(${COOP_TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/coop-net-test
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endforeach()
if(NOT MSVC)
    target_compile_options(coop_net_test_asan PRIVATE
        -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(coop_net_test_asan PRIVATE -fsanitize=address,undefined)
endif()
