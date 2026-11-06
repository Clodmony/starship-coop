// Test switches for the determinism spike (WP-02). See switches.h.

#include "switches.h"
#include "team_trace.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <libultraship/luslog.h>

// Sample counts per audio update, chosen each update from the output device's fill level
// (src/port/Engine.cpp:363-364, :387-388).
extern unsigned short samples_high;
extern unsigned short samples_low;

static int CoopTestSwitches_IsSet(const char* value) {
    return (value != NULL) && (value[0] != '\0') && (strcmp(value, "0") != 0);
}

void CoopTestSwitches_Init(void) {
    const char* noFma3 = getenv("SF64_COOP_TEST_NO_FMA3");
    const char* audioSamples = getenv("SF64_COOP_TEST_AUDIO_SAMPLES");

    if (CoopTestSwitches_IsSet(noFma3)) {
#if defined(_MSC_VER) && defined(_M_X64)
        // _get_FMA3_enable() is 1 only when the CPU has FMA3 and the runtime uses it, so the log shows whether this
        // switch changes anything on this PC.
        int before = _get_FMA3_enable();
        _set_FMA3_enable(0);
        LUSLOG_INFO("coop: test switch: FMA3 in the C runtime math functions %d -> %d", before, _get_FMA3_enable());
#else
        LUSLOG_INFO("coop: test switch SF64_COOP_TEST_NO_FMA3 has no effect on this platform%s", "");
#endif
    }

    if (CoopTestSwitches_IsSet(audioSamples)) {
        if (strcmp(audioSamples, "low") == 0) {
            samples_high = samples_low;
        } else if (strcmp(audioSamples, "high") == 0) {
            samples_low = samples_high;
        }
        LUSLOG_INFO("coop: test switch: audio samples per update high %u, low %u", samples_high, samples_low);
    }

    CoopTeamTrace_Init(); // no-op unless SF64_COOP_TEAM_* is set (#89)
}
