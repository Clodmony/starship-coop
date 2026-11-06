#include "version_label.h"

#include <stdio.h>

#include <imgui.h>

#include "port/coop/session/identity_game.h"

void CoopUi_DrawVersionLabel(void) {
    char label[64];

    snprintf(label, sizeof(label), "Co-op %s", CoopIdentity_BuildId());

    const float x = ImGui::GetWindowWidth() - ImGui::CalcTextSize(label).x - ImGui::GetStyle().ItemSpacing.x;
    if (x < ImGui::GetCursorPosX()) {
        return;
    }
    ImGui::SetCursorPosX(x);
    ImGui::TextDisabled("%s", label);
}
