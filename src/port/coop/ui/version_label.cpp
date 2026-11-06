#include "version_label.h"

#include <stdio.h>

#include <imgui.h>

#include "port/coop/session/identity_game.h"

void CoopUi_DrawVersionLabel(void) {
    char label[64];

    // The first 8 characters: with the Co-op menu in the bar, all 12 do not fit the default window of 640.
    snprintf(label, sizeof(label), "Co-op %.8s", CoopIdentity_BuildId());

    const float x = ImGui::GetWindowWidth() - ImGui::CalcTextSize(label).x - ImGui::GetStyle().ItemSpacing.x;
    if (x < ImGui::GetCursorPosX()) {
        return;
    }
    ImGui::SetCursorPosX(x);
    ImGui::TextDisabled("%s", label);
}
