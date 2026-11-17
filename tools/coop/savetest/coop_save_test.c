// Tests what a session earns (decision 0017, K2): src/port/coop/game/save.c with the game's own
// src/engine/fox_save.c, no game around them. tools/coop/save_merge_test.py compiles this file with those two, makes
// the save files (make_save.py) and reads what this program prints and writes.
//
//   coop_save_test <own save file> <result file> <step>...
//
// The own save file is this game's default.sav: it is read at the start as the game reads it at boot, and it is the
// only thing Save_WriteData below writes to, as src/sys/sys_joybus.c does: nothing while CoopSave_WriteBlocked().
// Steps, in order:
//
//   enter:<file>     CoopSave_Enter with the first save of that file, as SESSION_SAVE carries the host's
//   enter_own        the host's own game: CoopSave_FromGame, then CoopSave_Enter with it
//   set:<slot>:<hex> a level's end: those bits into the planet byte of the session's copy, then Save_Write
//   rank             a run's end: bytes of the ranking table and the ending-seen byte changed, then Save_Write
//   break            the own save file is damaged from outside, both of its checksums
//   leave            CoopSave_Leave
//
// Prints `log <line>` for every line of the co-op log, then `blocked=<n>` (Save_Write calls that wrote nothing),
// `written=<n>` (the ones that reached the file) and `written_in_session=<n>` (of those, the ones between an enter and
// its leave), `kept_after_enter=<CoopSave_KeptLine>` after each enter, `kept=<CoopSave_KeptLine>` at the end and
// `memory=<gSaveFile.save as hex>`. The result file gets the own save file as it is at the end.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "port/coop/game/save.h"
#include "port/coop/log/coop_log.h"

SaveFile gSaveIOBuffer;

static SaveFile sFile; // the own save file
static int sBlocked = 0;
static int sWritten = 0;
static int sWrittenInSession = 0;
static bool sInSession = false;

bool Save_ReadData(void) {
    gSaveIOBuffer = sFile;
    return true;
}

// src/sys/sys_joybus.c: `return CoopSave_WriteBlocked() || (Save_WriteEeprom(&gSaveIOBuffer) == 0);`
bool Save_WriteData(void) {
    if (CoopSave_WriteBlocked()) {
        sBlocked++;
        return true;
    }
    sFile = gSaveIOBuffer;
    sWritten++;
    if (sInSession) {
        sWrittenInSession++;
    }
    return true;
}

void CoopLog_Write(CoopLogCategory category, bool verboseOnly, const char* format, ...) {
    va_list args;

    (void) category;
    (void) verboseOnly;
    printf("log ");
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

static void Die(const char* what, const char* detail) {
    printf("FAIL %s %s\n", what, detail);
    exit(2);
}

static void Read_File(const char* path, void* out, size_t size) {
    FILE* f = fopen(path, "rb");

    if ((f == NULL) || (fread(out, 1, size, f) != size)) {
        Die("cannot read", path);
    }
    fclose(f);
}

int main(int argc, char** argv) {
    uint8_t sent[COOP_SAVE_SIZE];
    FILE* out;
    int i;
    size_t b;

    if (argc < 3) {
        Die("usage:", "coop_save_test <own save file> <result file> <step>...");
    }
    Read_File(argv[1], &sFile, sizeof(sFile));
    // As the game at boot (src/engine/fox_game.c, GSTATE_CHECK_SAVE): a save that cannot be read becomes gDefaultSave.
    if (Save_Read() != 0) {
        gSaveFile.save = gDefaultSave;
        gSaveFile.backup = gDefaultSave;
        Save_Write();
    }
    sWritten = 0;
    for (i = 3; i < argc; i++) {
        const char* step = argv[i];
        unsigned slot;
        unsigned bits;

        if (strncmp(step, "enter:", 6) == 0) {
            Read_File(step + 6, sent, sizeof(sent));
            CoopSave_Enter(sent);
            sInSession = true;
            printf("kept_after_enter=%s\n", CoopSave_KeptLine());
        } else if (strcmp(step, "enter_own") == 0) {
            CoopSave_FromGame(sent);
            CoopSave_Enter(sent);
            sInSession = true;
            printf("kept_after_enter=%s\n", CoopSave_KeptLine());
        } else if (sscanf(step, "set:%u:%x", &slot, &bits) == 2) {
            if ((slot >= sizeof(gSaveFile.save.data.planet)) || (bits > 0xFF)) {
                Die("bad step", step);
            }
            gSaveFile.save.raw[slot] |= (u8) bits;
            Save_Write();
        } else if (strcmp(step, "rank") == 0) {
            gSaveFile.save.data.rankNameEntry[0][0] = 'Z';
            gSaveFile.save.data.rankingRoute[0] = 7;
            gSaveFile.save.data.rankingLives[0] = 9;
            gSaveFile.save.data.rankingMedal[0] = 0x7F;
            gSaveFile.save.data.stats[0][0].hitCount = 200;
            gSaveFile.save.data.padEE[0] = 1; // src/overlays/ovl_ending/fox_end2.c: the ending was seen
            Save_Write();
        } else if (strcmp(step, "break") == 0) {
            sFile.save.checksum ^= 0x55;
            sFile.backup.checksum ^= 0x55;
        } else if (strcmp(step, "leave") == 0) {
            sInSession = false; // the write that keeps what was earned is the leave's own
            CoopSave_Leave();
        } else {
            Die("unknown step", step);
        }
    }
    printf("blocked=%d\nwritten=%d\nwritten_in_session=%d\n", sBlocked, sWritten, sWrittenInSession);
    printf("kept=%s\n", CoopSave_KeptLine());
    printf("memory=");
    for (b = 0; b < sizeof(gSaveFile.save); b++) {
        printf("%02x", ((const u8*) &gSaveFile.save)[b]);
    }
    printf("\n");
    out = fopen(argv[2], "wb");
    if ((out == NULL) || (fwrite(&sFile, 1, sizeof(sFile), out) != sizeof(sFile))) {
        Die("cannot write", argv[2]);
    }
    fclose(out);
    return 0;
}
