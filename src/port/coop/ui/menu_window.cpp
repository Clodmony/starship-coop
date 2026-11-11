// The co-op menu in ImGui, see menu_window.h.

#include "menu_window.h"

#ifdef USE_NETWORKING

#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>

#include <SDL2/SDL.h>
#include <imgui.h>
#include <libultraship/libultraship.h>

#include "menu.h"
#include "menu_game.h"
#include "port/coop/game/level_start.h"
#include "port/coop/game/names.h"
#include "port/coop/session/identity_game.h"
#include "port/coop/test/menu_driver.h"
#include "port/ui/UIWidgets.h"

namespace {

// Holds a port and the end of the string.
constexpr size_t kPortSize = 6;
// Keeps the button in a menu with nothing else in it from shrinking to its label.
constexpr float kMenuButtonWidth = 200.0f;
const char* const kWindowCvar = "gCoopMenuWindow";
// The player's name (#258), kept in the co-op settings between sessions: the config of the game that runs.
const char* const kNameCvar = "gCoopName";

const CoopMenuBackend kBackend = { CoopNetSdl_Host,           CoopNetSdl_Connect,   CoopNetSdl_Port,
                                   CoopIdentity_FromGame,     CoopMenuGame_Level,   CoopMenuGame_EnterLevel,
                                   CoopMenuGame_SessionEnded, CoopMenuGame_Message, CoopMenuGame_Waiting,
                                   CoopMenuGame_Handover,     CoopNames_Set };

CoopMenu sMenu;
char sAddress[COOP_MENU_ADDRESS_SIZE];
char sPort[kPortSize];
char sName[COOP_NAME_MAX + 1];

class MenuWindow : public Ship::GuiWindow {
  public:
    using Ship::GuiWindow::GuiWindow;

  protected:
    void InitElement() override {
    }
    void DrawElement() override;
    // Gui::DrawMenu runs this every frame, with the window closed too. The session is not ticked here: see menu_game.h.
    void UpdateElement() override {
        CoopMenuDriver_Frame();
        if (CoopMenu_WantsEyes(&sMenu)) {
            Show();
        }
    }
};

std::shared_ptr<MenuWindow> sWindow;

bool Button(const char* id, const char* label, bool enabled = true) {
    char text[COOP_MENU_LINE_SIZE + 32];

    // The label changes with the state, the widget's ImGui id must not.
    snprintf(text, sizeof(text), "%s###%s", label, id);
    bool pressed = UIWidgets::Button(text, { .disabled = !enabled });

    CoopMenuDriver_Item(id, label, enabled);
    return pressed;
}

void Text(const char* id, const char* text) {
    ImGui::TextWrapped("%s", text);
    CoopMenuDriver_Text(id, text);
}

void DrawField(const char* id, const char* label, char* buffer, size_t size, ImGuiInputTextFlags flags) {
    char hidden[32];

    snprintf(hidden, sizeof(hidden), "##%s", id);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText(hidden, buffer, size, flags | ImGuiInputTextFlags_AutoSelectAll);
    CoopMenuDriver_Item(id, label, true);
}

// Takes only what the game's font can draw (COOP_NAME_CHARS), lower case as upper case.
int NameFilter(ImGuiInputTextCallbackData* data) {
    if (data->EventChar < 128) {
        data->EventChar = (ImWchar) toupper((int) data->EventChar);
    }
    return (data->EventChar >= 128 || data->EventChar == 0 || strchr(COOP_NAME_CHARS, (int) data->EventChar) == nullptr)
               ? 1
               : 0;
}

// The name the other players see over this player's Arwing and in the hits (#258), saved as it is typed.
void DrawName() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Your name");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##name", sName, sizeof(sName),
                         ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_AutoSelectAll, NameFilter)) {
        char clean[COOP_NAME_MAX + 1];

        CoopSession_CleanName(sName, clean);
        CVarSetString(kNameCvar, clean);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    CoopMenuDriver_Item("name", "Your name", true); // before the tooltip, which is an item of its own
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Up to %d letters, digits, spaces and - . ! :", COOP_NAME_MAX);
    }
}

void DrawIdle() {
    DrawName();
    UIWidgets::PaddedSeparator();
    DrawField("port", "Port", sPort, sizeof(sPort), ImGuiInputTextFlags_CharsDecimal);
    if (Button("host", "Host a game")) {
        CoopMenu_SetName(&sMenu, sName);
        CoopMenu_Host(&sMenu, sPort);
    }
    UIWidgets::PaddedSeparator();
    DrawField("address", "Host's address", sAddress, sizeof(sAddress), ImGuiInputTextFlags_None);
    if (Button("join", "Join a game")) {
        CoopMenu_SetName(&sMenu, sName);
        CoopMenu_Join(&sMenu, sAddress, sPort, SDL_GetTicks());
    }
}

void DrawPlayers(const CoopMenuView& view) {
    static const char* const ids[COOP_SESSION_PLAYERS] = { "player1", "player2", "player3", "player4" };

    for (int i = 0; i < COOP_SESSION_PLAYERS; i++) {
        if (view.players[i].present) {
            Text(ids[i], view.players[i].text);
        }
    }
}

void DrawChoices(const CoopMenuView& view) {
    static const char* const ids[COOP_MENU_CHOICES] = { "pick-falco", "pick-slippy", "pick-peppy" };

    UIWidgets::PaddedSeparator();
    ImGui::TextUnformatted("Your character");
    for (int i = 0; i < COOP_MENU_CHOICES; i++) {
        if (Button(ids[i], view.choices[i].label, view.choices[i].enabled)) {
            CoopMenu_Pick(&sMenu, view.choices[i].character);
        }
    }
}

// The host's dropdown (#211). Each entry is a widget of its own for menu_test.py, which clicks with the mouse.
void DrawStartLevel() {
    const int32_t current = CoopMenuGame_StartLevelIndex();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Start at");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    const bool open = ImGui::BeginCombo("##start-level", CoopMenuGame_StartLevelName(current));
    CoopMenuDriver_Item("start-level", CoopMenuGame_StartLevelName(current), true);
    if (open) {
        for (int32_t i = 0; i < CoopMenuGame_StartLevelCount(); i++) {
            char id[32];

            snprintf(id, sizeof(id), "start-level-%d", (int) i);
            if (ImGui::Selectable(CoopMenuGame_StartLevelName(i), i == current)) {
                CoopMenuGame_SetStartLevel(i);
            }
            CoopMenuDriver_Item(id, CoopMenuGame_StartLevelName(i), true);
        }
        ImGui::EndCombo();
    }
}

// Below the players and the characters, above Leave.
void DrawLevelStart(const CoopMenuView& view) {
    if (view.showStart) {
        UIWidgets::PaddedSeparator();
        DrawStartLevel();
        if (Button("start", view.startLabel, view.canStart)) {
            CoopMenu_Start(&sMenu);
        }
    }
    if (view.level[0] != '\0') {
        UIWidgets::PaddedSeparator();
        Text("level", view.level);
    }
}

void MenuWindow::DrawElement() {
    CoopMenuView view;

    CoopMenu_View(&sMenu, &view);
    Text("status", view.status);
    if (view.phase == COOP_MENU_IDLE) {
        DrawIdle();
    } else {
        DrawPlayers(view);
        if (view.showChoices) {
            DrawChoices(view);
        }
        DrawLevelStart(view);
        UIWidgets::PaddedSeparator();
        if (Button("leave", view.phase == COOP_MENU_CONNECTING ? "Cancel" : "Leave")) {
            CoopMenu_Leave(&sMenu);
        }
    }
    if (view.message[0] != '\0') {
        UIWidgets::PaddedSeparator();
        Text("message", view.message);
    }
}

} // namespace

namespace CoopUi {

void Setup() {
    CoopMenuDriver_Init();
    CoopMenu_Init(&sMenu, &kBackend);
    snprintf(sPort, sizeof(sPort), "%d", COOP_MENU_DEFAULT_PORT);
    CoopSession_CleanName(CVarGetString(kNameCvar, ""), sName);
    sWindow = std::make_shared<MenuWindow>(kWindowCvar, "Co-op", ImVec2(420.0f, 400.0f));
    Ship::Context::GetInstance()->GetWindow()->GetGui()->AddGuiWindow(sWindow);
}

void Listen() {
    CoopMenuGame_Listen(&sMenu);
}

void DrawMenuEntry() {
    bool open = UIWidgets::BeginMenu("Co-op");

    CoopMenuDriver_Item("menu", "Co-op", true);
    if (open) {
        // An open popup blocks the mouse in every other window, so the new window's first click would be lost.
        if (UIWidgets::WindowButton("Co-op window", kWindowCvar, sWindow,
                                    { .size = ImVec2(kMenuButtonWidth, 0.0f),
                                      .tooltip = "Host a game, join one and choose your character" })) {
            ImGui::CloseCurrentPopup();
        }
        CoopMenuDriver_Item("open", "Co-op window", true);
        ImGui::EndMenu();
    }
}

void Destroy() {
    CoopMenu_Leave(&sMenu);
    sWindow = nullptr;
}

} // namespace CoopUi

#else

namespace CoopUi {

void Setup() {
}

void Listen() {
}

void DrawMenuEntry() {
}

void Destroy() {
}

} // namespace CoopUi

#endif
