// Stands in for include/global.h in tools/coop/save_merge_test.py, so that src/port/coop/game/save.c and
// src/engine/fox_save.c compile without the game: the save's own headers, and the few names those two files use
// beside them. This folder comes first on the include path of that test and of nothing else.
#ifndef COOP_SAVE_TEST_GLOBAL_H
#define COOP_SAVE_TEST_GLOBAL_H

#include <libultra/types.h>

typedef struct {
    f32 x, y, z;
} Vec3f; // include/sf64math.h, which needs libultraship; sf64level.h uses it in a struct this test never touches

#include "sf64level.h" // PlanetId, SAVE_SLOT_*
#include "sf64save.h"  // Save, SaveFile, Save_Read, Save_Write

#define ARRAY_COUNT(arr) (s32)(sizeof(arr) / sizeof(arr[0])) // include/macros.h
#define PRINTF(...)
#define SOUNDMODE_STEREO 0 // include/sf64audio_external.h, the first of its enum

typedef void* OSMesg;

#endif
