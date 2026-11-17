#pragma once

// The session's save (decision 0009 item 8, S1 as the owner changed it on 2026-11-10). The host's progress is the save
// of a co-op session: at the level start the host's game hands its Save to every client (SESSION_SAVE,
// docs/coop/PROTOCOL.md), and every game, the host's too, plays the session on a copy of it in memory. No machine
// writes a real save while the session runs, and when it ends each game reads its own real save back from its file.
//
// What a session earns (decision 0017, K2, accepted 2026-11-17): every game, the host's too, keeps the planet flags the
// session earned. Earned is a flag (played, normal clear, normal medal, expert clear, expert medal) that the session's
// copy holds when the session ends and did not hold when the session began, so a player does not get what the host
// had before. Once, in CoopSave_Leave, after the game's own file is back in memory, those flags are ORed into the
// game's own planet bytes, and one Save_Write of the game writes the file; a session that earned nothing this save
// lacks writes nothing. An OR cannot take a flag away. The rest of the session's copy is dropped: the ranking table,
// the ending-seen byte, everything else. A medal earned with cheats on counts as in single player.
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
// until CoopSave_Leave. The first call of a session also notes the planet bytes the session begins with; a level start
// that comes again in the same session (a recovery) does not.
void CoopSave_Enter(const uint8_t save[COOP_SAVE_SIZE]);

// The session is over, however it ended: this game's real save back into memory from its file (Save_Read), as at boot,
// then the planet flags the session earned ORed into it and the file written once, if there are any (decision 0017).
// A file that cannot be read is left alone and nothing is kept. Nothing if no session save was entered; a second call
// does nothing.
void CoopSave_Leave(void);

// One line for the player after a session that added something to this game's own save: "Kept from this session:
// Corneria (medal), Meteo." Otherwise "", and "" again from the next session's level start on.
const char* CoopSave_KeptLine(void);

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
