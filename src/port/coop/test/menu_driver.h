#pragma once

// Lets tools/coop/menu_test.py use the real co-op menu in a game nobody sits at (WP-25). Off unless the environment
// variable is set when the game starts:
//   SF64_COOP_TEST_MENU=<folder>
// With it the game reads commands from <folder>/menu-commands.txt, one per line and added while it runs:
//   click <id>          the left mouse button on that widget, through ImGui's own input queue
//   type <id> <text>    a click on that field, then the text as typed characters
// and writes what the window holds to <folder>/menu-state.txt each time that changes:
//   frame <n>
//   item <id> <0 or 1, enabled> <label>
//   text <id> <text, new lines as " | ">
//   end
// A click on a widget that is not on screen writes "error <text>". A disabled widget gets the click like any other, so
// a test can see that it does nothing. Without the variable every function here returns at once.

void CoopMenuDriver_Init();
// Once per frame, before the window is drawn.
void CoopMenuDriver_Frame();
// After a widget: id names it in the commands and the state, label is what the player reads.
void CoopMenuDriver_Item(const char* id, const char* label, bool enabled);
void CoopMenuDriver_Text(const char* id, const char* text);
