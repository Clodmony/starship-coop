// The identity of the running game: the build id (written by tools/coop/cmake/coop_build_id.cmake), the game
// version of every loaded archive, the simulation CVars.

#include "identity_game.h"

#include <stdio.h>
#include <string.h>

#include <libultraship/bridge.h>

#include "coop_build_id.h"

const char* CoopIdentity_BuildId(void) {
    return COOP_BUILD_ID;
}

void CoopIdentity_FromGame(CoopIdentity* out) {
    uint32_t roms[16];
    size_t count = 0;
    size_t i;

    memset(out, 0, sizeof(*out));
    snprintf(out->build, sizeof(out->build), "%s", CoopIdentity_BuildId());
    CoopIdentity_Platform(out->platform, sizeof(out->platform));

    // Archives without a version file (starship.o2r) are not in this list (ArchiveManager::AddArchive).
    ResourceGetGameVersions(roms, sizeof(roms), &count);
    for (i = 0; i < count && i < sizeof(roms) / sizeof(roms[0]) && out->romCount < COOP_ID_MAX_ROMS; i++) {
        out->roms[out->romCount++] = roms[i];
    }
    CoopIdentity_SortRoms(out);

    out->cvarTable = CoopSimCvars_TableHash();
    out->cvarCount = CoopSimCvars_Count();
    CoopSimCvars_Read(out->cvars, CVarGetInteger, CVarGetFloat);
}
