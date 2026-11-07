# Test program for the co-op menu, driven by tools/coop/menu_test.py. Not part of the default build. EXCLUDE_FROM_ALL goes
# here, see tools/coop/nettest/CMakeLists.txt.
#
# coop_menu_test_asan is the same program under AddressSanitizer and UBSan (menu_test.py --asan). The flags are on this
# target alone, so it shares the normal build directory. MSVC has no UBSan, so there it does not exist.
set(COOP_MENU_TEST_TARGETS coop_menu_test)
if(NOT MSVC)
    list(APPEND COOP_MENU_TEST_TARGETS coop_menu_test_asan)
endif()
find_package(SDL2 REQUIRED)
foreach(COOP_TARGET ${COOP_MENU_TEST_TARGETS})
    add_executable(${COOP_TARGET} EXCLUDE_FROM_ALL
        coop_menu_test.c
        ${CMAKE_SOURCE_DIR}/src/port/coop/net/net_core.c
        ${CMAKE_SOURCE_DIR}/src/port/coop/net/transport_sdlnet.c
        ${CMAKE_SOURCE_DIR}/src/port/coop/session/identity.c
        ${CMAKE_SOURCE_DIR}/src/port/coop/session/session.c
        ${CMAKE_SOURCE_DIR}/src/port/coop/log/coop_log.c
        ${CMAKE_SOURCE_DIR}/src/port/coop/session/sim_cvars.c
        ${CMAKE_SOURCE_DIR}/src/port/coop/ui/menu.c
    )
    target_include_directories(${COOP_TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(${COOP_TARGET} PRIVATE USE_NETWORKING)
    target_link_libraries(${COOP_TARGET} PRIVATE ${COOP_SDL2_NET}
        "$<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>")
    set_target_properties(${COOP_TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/coop-menu-test
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endforeach()
if(NOT MSVC)
    target_compile_options(coop_menu_test_asan PRIVATE
        -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(coop_menu_test_asan PRIVATE -fsanitize=address,undefined)
endif()
