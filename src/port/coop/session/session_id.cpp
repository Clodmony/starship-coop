// A session id, see session_id.h.

#include "session_id.h"

#include <chrono>
#include <random>

extern "C" uint64_t CoopSessionId_New(void) {
    uint64_t id = 0;

    try {
        std::random_device device;

        id = (static_cast<uint64_t>(device()) << 32) ^ static_cast<uint64_t>(device());
    } catch (...) {
        // No random source on this system: the clocks alone still tell two sessions of one host apart.
    }
    id ^= static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) * 0x9E3779B97F4A7C15ull;
    id ^= static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    return (id != 0) ? id : 1;
}
