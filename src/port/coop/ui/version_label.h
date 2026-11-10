#pragma once

// Which co-op build this is, at the right end of the menu bar (#26). The id is the one the handshake compares, so two
// players can see whether they run the same build before they connect. Tested by tools/coop/version_test.py. The text,
// with "-dirty" for a build made from uncommitted changes, is CoopUi_VersionLabelText (version_text.h, #232).

#ifdef __cplusplus
extern "C" {
#endif

// Call between ImGui::BeginMenuBar and ImGui::EndMenuBar, after the last menu. Touches ImGui only, nothing the
// simulation reads. In a window with no room after the menus, it draws nothing.
void CoopUi_DrawVersionLabel(void);

#ifdef __cplusplus
}
#endif
