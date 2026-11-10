// Where libultraship writes its log, for the crash report of stop_signal.c (#158).

#include "stop_signal_log.h"

#include <cstdio>
#include <string>

#include <libultraship/libultraship.h>

extern "C" void CoopStopSignal_LogPath(char* out, size_t size) {
    const auto context = Ship::Context::GetInstance();

    if ((out == nullptr) || (size == 0)) {
        return;
    }
    out[0] = '\0';
    if (context == nullptr) {
        return;
    }
    // As libultraship's InitLogging names it (libultraship/src/Context.cpp:144).
    const std::string path = Ship::Context::GetPathRelativeToAppDirectory("logs/" + context->GetName() + ".log");
    std::snprintf(out, size, "%s", path.c_str());
}
