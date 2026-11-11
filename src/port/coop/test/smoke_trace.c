#include "smoke_trace.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "global.h"

#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#define SMOKE_TRACE_HAS_BACKTRACE 1
#else
#define SMOKE_TRACE_HAS_BACKTRACE 0
#endif

#define SMOKE_TRACE_DEPTH 8 // this function, the setup, then up to six callers

void EffectFireSmoke_Setup(EffectFireSmoke* this, f32 xPos, f32 yPos, f32 zPos, f32 scale2);

static FILE* sFile = NULL;

void CoopSmokeTrace_Note(const struct Effect* effect) {
    if (sFile == NULL) {
        return;
    }
    fprintf(sFile,
            "smoke frame=%d level=%d slot=%d pos=%.2f,%.2f,%.2f scale=%.2f path=%.1f callers=", (int) gGameFrameCount,
            (int) gCurrentLevel, (int) (effect - gEffects), effect->obj.pos.x, effect->obj.pos.y, effect->obj.pos.z,
            effect->scale2, gPathProgress);
#if SMOKE_TRACE_HAS_BACKTRACE
    {
        void* frames[SMOKE_TRACE_DEPTH];
        int count = backtrace(frames, SMOKE_TRACE_DEPTH);
        int i;

        // frames[0] is this function and frames[1] the setup that called it.
        for (i = 2; i < count; i++) {
            fprintf(sFile, "%s%+lld", (i > 2) ? "," : "",
                    (long long) ((intptr_t) frames[i] - (intptr_t) &EffectFireSmoke_Setup));
        }
    }
#endif
    fputc('\n', sFile);
    fflush(sFile); // tests stop the game from outside
}

void CoopSmokeTrace_Init(void) {
    const char* path = getenv("SF64_COOP_SMOKE_TRACE");

    if ((path == NULL) || (path[0] == '\0')) {
        return;
    }
    sFile = fopen(path, "a");
}
