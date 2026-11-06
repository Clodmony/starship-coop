#pragma once

// The co-op menu in ImGui (WP-25): the "Co-op" entry in the menu bar and the window it opens. This file only draws;
// what the window can do and what it shows is in menu.h. Builds without networking get empty functions and no entry.

namespace CoopUi {

// Once the gui exists: adds the window, closed unless the player left it open.
void Setup();
// Once the game's events exist, which is after Setup: from here on the session runs with the game's frames.
void Listen();
// Inside the menu bar.
void DrawMenuEntry();
// Closes the session, if there is one.
void Destroy();

} // namespace CoopUi
