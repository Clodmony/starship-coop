#pragma once

#include <stdbool.h>
#include <stddef.h>

// The text of the version label (version_label.h), apart from ImGui so tools/coop/version_test.py can compile it alone.
// The build id is a commit's first 12 characters, with "-dirty" after them for a build made from uncommitted changes
// (tools/coop/cmake/coop_build_id.cmake). The label shows the first 8 characters, and the "-dirty" ending (#232):
// - full:  "Co-op <8>", or "Co-op <8>-dirty";
// - short: "<8>-dirty", for a dirty id in a bar with no room for the full one. A clean id has no short form.

#ifdef __cplusplus
extern "C" {
#endif

// Writes the full or the short label for `buildId` into `out` (always terminated). Returns false, with `out` empty,
// when there is no such form.
bool CoopUi_VersionLabelText(const char* buildId, bool shortForm, char* out, size_t size);

#ifdef __cplusplus
}
#endif
