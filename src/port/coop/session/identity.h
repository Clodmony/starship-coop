#pragma once

// What a player's game must agree on with the host's before they play together (PROTOCOL.md rule 2): the build, the
// ROM and the simulation CVars. The platform is compared too, but only warns (decision 0010): lockstep needs every
// client to compute the same game, and nobody has compared two platforms yet, so the desync check (WP-23) is what
// tells whether two platforms do.
// Plain C with no game or network dependency, so the test program can build identities of its own.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sim_cvars.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COOP_ID_TEXT 48
#define COOP_ID_MAX_ROMS 8

typedef struct {
    char build[COOP_ID_TEXT];    // git commit, "-dirty" appended when the build had uncommitted changes
    char platform[COOP_ID_TEXT]; // OS, CPU, compiler, see CoopIdentity_Platform
    uint8_t romCount;
    uint32_t roms[COOP_ID_MAX_ROMS]; // game version of each loaded archive (the ROM's header CRC), sorted
    uint32_t cvarTable;              // CoopSimCvars_TableHash
    uint16_t cvarCount;
    int32_t cvars[COOP_SIM_CVARS_MAX];
} CoopIdentity;

typedef enum {
    COOP_ID_DIFF_BUILD = 1 << 0,
    COOP_ID_DIFF_ROM = 1 << 1,
    COOP_ID_DIFF_CVARS = 1 << 2,
    COOP_ID_DIFF_PLATFORM = 1 << 3,
} CoopIdentityDiff;

// Refuses the connection. The rest of COOP_ID_DIFF_ALL only warns, on both sides.
#define COOP_ID_DIFF_REFUSE (COOP_ID_DIFF_BUILD | COOP_ID_DIFF_ROM | COOP_ID_DIFF_CVARS)
#define COOP_ID_DIFF_ALL (COOP_ID_DIFF_REFUSE | COOP_ID_DIFF_PLATFORM)

// The platform this file was compiled for, for example "Windows x86-64 MSVC 1944".
void CoopIdentity_Platform(char* out, size_t cap);
// Sorts roms; the order archives were loaded in does not matter.
void CoopIdentity_SortRoms(CoopIdentity* id);

uint32_t CoopIdentity_Compare(const CoopIdentity* a, const CoopIdentity* b);

// The wire form, see PROTOCOL.md. Write returns the size, 0 if cap is too small. Read checks every length.
uint16_t CoopIdentity_Write(const CoopIdentity* id, uint8_t* out, uint16_t cap);
// Returns the bytes it read, 0 if data is not a valid identity.
uint16_t CoopIdentity_Read(CoopIdentity* id, const uint8_t* data, uint16_t size);

// A message the player can read, in English like the rest of the game's menus: what differs, on whose side. One line
// per difference. host and joiner name the two sides; the caller's own side is "you" in neither, so both players get
// the same text.
size_t CoopIdentity_Describe(uint32_t diff, const CoopIdentity* host, const CoopIdentity* joiner, char* out,
                             size_t cap);

// A name for a ROM version, for example "US 1.0", or NULL if it is not known.
const char* CoopIdentity_RomName(uint32_t version);

// One line for a report: the build, the platform, the ROMs and how many simulation CVars it carries. Nothing personal.
void CoopIdentity_Line(const CoopIdentity* id, char* out, size_t cap);

#ifdef __cplusplus
}
#endif
