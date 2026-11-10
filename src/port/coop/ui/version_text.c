#include "version_text.h"

#include <stdio.h>
#include <string.h>

#define DIRTY "-dirty"

bool CoopUi_VersionLabelText(const char* buildId, bool shortForm, char* out, size_t size) {
    size_t length = strlen(buildId);
    bool dirty = (length >= strlen(DIRTY)) && (strcmp(buildId + length - strlen(DIRTY), DIRTY) == 0);

    if (size == 0) {
        return false;
    }
    out[0] = '\0';
    if (shortForm && !dirty) {
        return false;
    }
    // The first 8 characters: with the Co-op menu in the bar, all 12 do not fit the default window of 640.
    snprintf(out, size, "%s%.8s%s", shortForm ? "" : "Co-op ", buildId, dirty ? DIRTY : "");
    return true;
}
