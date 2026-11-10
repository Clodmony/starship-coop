#include "version_label.h"

#include <imgui.h>

#include "port/coop/session/identity_game.h"
#include "version_text.h"

// Where `label` starts at the right end of the bar, false when it does not fit after the menus.
static bool LabelFits(const char* label, float* x) {
    *x = ImGui::GetWindowWidth() - ImGui::CalcTextSize(label).x - ImGui::GetStyle().ItemSpacing.x;
    return *x >= ImGui::GetCursorPosX();
}

void CoopUi_DrawVersionLabel(void) {
    char label[64];
    float x;

    // The full label, or for a dirty build the short one, so that "-dirty" shows in the default window of 640 too.
    CoopUi_VersionLabelText(CoopIdentity_BuildId(), false, label, sizeof(label));
    if (!LabelFits(label, &x) &&
        !(CoopUi_VersionLabelText(CoopIdentity_BuildId(), true, label, sizeof(label)) && LabelFits(label, &x))) {
        return;
    }
    ImGui::SetCursorPosX(x);
    ImGui::TextDisabled("%s", label);
}
