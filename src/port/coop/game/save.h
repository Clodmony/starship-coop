#pragma once

// The session's save (decision 0009 item 8, S1 as the owner changed it on 2026-11-10). The host's progress is the save
// of a co-op session: at the level start the host's game hands its Save to every client (SESSION_SAVE,
// docs/coop/PROTOCOL.md), and every game, the host's too, plays the session on a copy of it in memory. No machine
// writes a real save while the session runs, and when it ends each game reads its own real save back from its file, so
// what the session did is dropped: medals and clears earned in a session are not kept.
//
// Why: the simulation reads the save. The map after a level takes another way when the planet was not played before
// (src/overlays/ovl_menu/fox_map.c, D_menu_801CD980), the intro and the briefing can be skipped only on a cleared or
// played planet, and two PCs hold two different saves (docs/coop/NOTES.md, 2026-11-10).
//
// A client keeps its own settings, which live in the same struct (include/sf64save.h): the sound mode and the three
// volumes, and the two language bytes no code of the port reads. Everything else is the host's: planet flags, medals,
// rankings, the ending-seen flag. The settings are no simulation: GSTATE_INIT and the option menu hand them to the
// audio thread, and the state hash leaves them out.
//
// Co-op off: nothing here runs, and Save_WriteData writes as upstream.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A save as the game writes it to its file: 254 bytes of data (SaveData), then the checksum, low byte first.
#define COOP_SAVE_SIZE 256

// Host: this game's save as it is in memory, with a checksum of what is sent.
void CoopSave_FromGame(uint8_t out[COOP_SAVE_SIZE]);

// Every game, the host's too, at the level start of a session, before the level: the session's save is the progress of
// `save` with this game's own settings, in gSaveFile and its backup copy. From here on Save_WriteData writes nothing,
// until CoopSave_Leave.
void CoopSave_Enter(const uint8_t save[COOP_SAVE_SIZE]);

// The session is over, however it ended: this game's real save back into memory from its file (Save_Read), as at boot.
// Nothing if no session save was entered; a second call does nothing.
void CoopSave_Leave(void);

// Nonzero from CoopSave_Enter to CoopSave_Leave: what the hook in Save_WriteData (src/sys/sys_joybus.c) reads.
int32_t CoopSave_WriteBlocked(void);

// The bytes of a save that are the session's, for the state hash: everything but the settings and the checksum.
typedef struct {
    uint16_t offset;
    uint16_t size;
    const char* name;
} CoopSaveRange;

// The ranges, in save order; returns how many.
int32_t CoopSave_ProgressRanges(const CoopSaveRange** ranges);

#ifdef __cplusplus
}
#endif
