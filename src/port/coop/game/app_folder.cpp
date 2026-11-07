// The game's own folder, see app_folder.h.

#include "app_folder.h"

#include <cstring>
#include <string>

#include "libultraship/src/Context.h"

extern "C" void CoopAppFolder_Path(char* out, size_t cap) {
    std::string path = Ship::Context::GetAppDirectoryPath();

    while (!path.empty() && ((path.back() == '/') || (path.back() == '\\'))) {
        path.pop_back();
    }
    if (cap == 0) {
        return;
    }
    std::strncpy(out, path.c_str(), cap - 1);
    out[cap - 1] = '\0';
}
