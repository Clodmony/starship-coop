# Test program for the Steam relay transport, driven by tools/coop/steam_test.py. Not part of the default build, and
# built without the Steamworks SDK: the transport runs on a fake, the real backend loads Steam's library at run time.
# EXCLUDE_FROM_ALL goes here, see tools/coop/nettest/CMakeLists.txt.
#
# coop_steam_test_asan is the same program under AddressSanitizer and UBSan (steam_test.py --asan). The flags are on
# this target alone, so it shares the normal build directory. MSVC has no UBSan, so there it does not exist.
set(COOP_STEAM_TEST_TARGETS coop_steam_test)
if(NOT MSVC)
    list(APPEND COOP_STEAM_TEST_TARGETS coop_steam_test_asan)
endif()
find_package(SDL2 REQUIRED)
foreach(COOP_TARGET ${COOP_STEAM_TEST_TARGETS})
    add_executable(${COOP_TARGET} EXCLUDE_FROM_ALL
        coop_steam_test.c
        fake_steam.c
        ${CMAKE_SOURCE_DIR}/src/port/coop/net/steam_backend_flat.c
        ${CMAKE_SOURCE_DIR}/src/port/coop/net/transport_steam.c
        ${CMAKE_SOURCE_DIR}/src/port/coop/session/identity.c
        ${CMAKE_SOURCE_DIR}/src/port/coop/session/session.c
        ${CMAKE_SOURCE_DIR}/src/port/coop/session/sim_cvars.c
    )
    target_include_directories(${COOP_TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${COOP_TARGET} PRIVATE USE_NETWORKING USE_STEAM_RELAY)
    target_link_libraries(${COOP_TARGET} PRIVATE "$<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>")
    set_target_properties(${COOP_TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/coop-steam-test
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endforeach()
if(NOT MSVC)
    target_compile_options(coop_steam_test_asan PRIVATE
        -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(coop_steam_test_asan PRIVATE -fsanitize=address,undefined)
endif()

# The mock Steam library, in its current form and without SteamAPI_InitFlat. Uses POSIX sockets.
if(NOT WIN32)
    foreach(COOP_MOCK coop_steam_mock coop_steam_mock_old)
        add_library(${COOP_MOCK} MODULE EXCLUDE_FROM_ALL mock_steam_api.c)
        set_target_properties(${COOP_MOCK} PROPERTIES PREFIX "lib" OUTPUT_NAME steam_api SUFFIX ".so")
        foreach(COOP_TARGET ${COOP_STEAM_TEST_TARGETS})
            add_dependencies(${COOP_TARGET} ${COOP_MOCK})
        endforeach()
    endforeach()
    set_target_properties(coop_steam_mock PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/coop-steam-test/mock)
    set_target_properties(coop_steam_mock_old PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/coop-steam-test/mock-old)
    target_compile_definitions(coop_steam_mock_old PRIVATE MOCK_OLD_LIBRARY)
endif()
