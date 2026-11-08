// Drives the co-op menu for tools/coop/menu_test.py, see menu_driver.h.

#ifdef USE_NETWORKING

#include "menu_driver.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include <imgui.h>

namespace {

struct Item {
    std::string label;
    bool enabled;
    ImVec2 center;
    int frame;
};

struct Text {
    std::string text;
    int frame;
};

// One step per frame: ImGui takes a press only on something the mouse already hovers.
enum class Step { None, Press, Release, Type };

bool sActive;
std::string sCommandFile;
std::string sStateFile;
std::string sStallFile;
// Session ticks: the time of the last one with a session open, the longest gap so far, and the count.
bool sTicking;
uint32_t sLastTickMs;
uint32_t sLongestGapMs;
uint32_t sTicks;
long sCommandOffset;
std::map<std::string, Item> sItems;
std::map<std::string, Text> sTexts;
std::string sLastState;
Step sStep;
std::string sTyping;

void Append(const std::string& file, const std::string& line) {
    if (FILE* f = fopen(file.c_str(), "a")) {
        fputs(line.c_str(), f);
        fclose(f);
    }
}

void Write_Line(const std::string& line) {
    Append(sStateFile, line);
}

// False if the file holds no whole new line.
bool Read_Command(std::string* out) {
    FILE* f = fopen(sCommandFile.c_str(), "r");
    char line[512];
    bool whole = false;

    if (f == nullptr) {
        return false;
    }
    fseek(f, sCommandOffset, SEEK_SET);
    if (fgets(line, sizeof(line), f) != nullptr) {
        *out = line;
        whole = !out->empty() && out->back() == '\n';
    }
    if (whole) {
        sCommandOffset = ftell(f);
        out->pop_back();
    }
    fclose(f);
    return whole;
}

// The window draws after CoopMenuDriver_Frame and the menu bar before it, so what is on screen dates from this frame or
// the one before.
bool On_Screen(int drawn) {
    return drawn >= ImGui::GetFrameCount() - 1;
}

void Start_Click(const std::string& id) {
    auto item = sItems.find(id);

    if (item == sItems.end() || !On_Screen(item->second.frame)) {
        Write_Line("error no widget " + id + " on screen\n");
        sTyping.clear();
        return;
    }
    ImGui::GetIO().AddMousePosEvent(item->second.center.x, item->second.center.y);
    sStep = Step::Press;
}

void Next_Command() {
    std::string command;

    if (!Read_Command(&command)) {
        return;
    }
    size_t verbEnd = command.find(' ');
    std::string verb = command.substr(0, verbEnd);
    std::string rest = verbEnd == std::string::npos ? "" : command.substr(verbEnd + 1);

    if (verb == "click") {
        Start_Click(rest);
    } else if (verb == "type") {
        size_t idEnd = rest.find(' ');

        sTyping = idEnd == std::string::npos ? "" : rest.substr(idEnd + 1);
        Start_Click(rest.substr(0, idEnd));
    } else {
        Write_Line("error unknown command " + command + "\n");
    }
}

void Write_State() {
    std::string state;

    for (const auto& [id, item] : sItems) {
        if (On_Screen(item.frame)) {
            state += "item " + id + " " + (item.enabled ? "1 " : "0 ") + item.label + "\n";
        }
    }
    for (const auto& [id, text] : sTexts) {
        if (On_Screen(text.frame)) {
            state += "text " + id + " " + text.text + "\n";
        }
    }
    if (state != sLastState) {
        sLastState = state;
        Write_Line("frame " + std::to_string(ImGui::GetFrameCount()) + "\n" + state + "end\n");
    }
}

} // namespace

void CoopMenuDriver_Init(void) {
    const char* folder = getenv("SF64_COOP_TEST_MENU");

    if (folder != nullptr && folder[0] != '\0') {
        sActive = true;
        sCommandFile = std::string(folder) + "/menu-commands.txt";
        sStateFile = std::string(folder) + "/menu-state.txt";
        sStallFile = std::string(folder) + "/menu-stall.txt";
    }
}

void CoopMenuDriver_Tick(uint32_t nowMs, bool inSession) {
    if (!sActive) {
        return;
    }
    sTicks++;
    if (!inSession) {
        sTicking = false;
        return;
    }
    if (sTicking && nowMs - sLastTickMs > sLongestGapMs) {
        sLongestGapMs = nowMs - sLastTickMs;
        Append(sStallFile, "stall " + std::to_string(sLongestGapMs) + " tick " + std::to_string(sTicks) + "\n");
    }
    sTicking = true;
    sLastTickMs = nowMs;
}

void CoopMenuDriver_Frame(void) {
    ImGuiIO* io;

    if (!sActive) {
        return;
    }
    io = &ImGui::GetIO();
    switch (sStep) {
        case Step::None:
            Next_Command();
            break;
        case Step::Press:
            io->AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            sStep = Step::Release;
            break;
        case Step::Release:
            io->AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            sStep = sTyping.empty() ? Step::None : Step::Type;
            break;
        case Step::Type:
            io->AddInputCharactersUTF8(sTyping.c_str());
            sTyping.clear();
            sStep = Step::None;
            break;
    }
    Write_State();
}

void CoopMenuDriver_Item(const char* id, const char* label, bool enabled) {
    ImVec2 min;
    ImVec2 max;

    if (!sActive) {
        return;
    }
    min = ImGui::GetItemRectMin();
    max = ImGui::GetItemRectMax();
    sItems[id] = { label, enabled, ImVec2((min.x + max.x) / 2, (min.y + max.y) / 2), ImGui::GetFrameCount() };
}

void CoopMenuDriver_Text(const char* id, const char* text) {
    std::string flat;

    if (!sActive) {
        return;
    }
    for (const char* c = text; *c != '\0'; c++) {
        if (*c == '\n') {
            flat += " | ";
        } else {
            flat += *c;
        }
    }
    sTexts[id] = { flat, ImGui::GetFrameCount() };
}

#endif
