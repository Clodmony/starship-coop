// The session's save (decision 0009 item 8). See save.h.

#include "save.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "port/coop/log/coop_log.h"

// src/engine/fox_save.c
u16 Save_Checksum(Save* arg0);

#define SAVE_DATA_SIZE ((uint16_t) sizeof(SaveData))

// What a client keeps of its own: the sound mode and the volumes (0x14 to 0x17), the two language bytes (0xEB, 0xEC),
// and the checksum, which follows from the rest. Everything between is the session's.
static const CoopSaveRange sProgress[] = {
    { offsetof(SaveData, planet), offsetof(SaveData, soundMode) - offsetof(SaveData, planet),
      "gSaveFile.planet" }, // planet[16], pad10
    { offsetof(SaveData, rankNameEntry), offsetof(SaveData, textLanguage) - offsetof(SaveData, rankNameEntry),
      "gSaveFile.ranking" }, // rankNameEntry to unk_EA
    { offsetof(SaveData, padEE), sizeof(SaveData) - offsetof(SaveData, padEE), "gSaveFile.padEE" }, // [0]: ending seen
};

_Static_assert(sizeof(Save) == COOP_SAVE_SIZE, "a Save is not COOP_SAVE_SIZE bytes");
_Static_assert(offsetof(Save, checksum) == sizeof(SaveData), "the checksum does not follow the data");
_Static_assert(offsetof(SaveData, soundMode) == 0x14 && offsetof(SaveData, sfxVolume) == 0x17 &&
                   offsetof(SaveData, rankNameEntry) == 0x18,
               "the settings are not where the ranges leave them out");
_Static_assert(offsetof(SaveData, textLanguage) == 0xEB && offsetof(SaveData, padEE) == 0xED,
               "the language bytes are not where the ranges leave them out");

static bool sEntered = false;

int32_t CoopSave_ProgressRanges(const CoopSaveRange** ranges) {
    *ranges = sProgress;
    return (int32_t) ARRAY_COUNT(sProgress);
}

int32_t CoopSave_WriteBlocked(void) {
    return sEntered;
}

void CoopSave_FromGame(uint8_t out[COOP_SAVE_SIZE]) {
    Save copy = gSaveFile.save;

    copy.checksum = Save_Checksum(&copy);
    memcpy(out, copy.raw, SAVE_DATA_SIZE);
    out[SAVE_DATA_SIZE] = (uint8_t) copy.checksum;
    out[SAVE_DATA_SIZE + 1] = (uint8_t) (copy.checksum >> 8);
}

// The 16 planet bytes as hex, for the co-op log: who holds what is the first thing to read when a map parts.
static void Planets_Text(const u8* raw, char* out, size_t cap) {
    size_t used = 0;
    s32 i;

    out[0] = '\0';
    for (i = 0; (i < (s32) ARRAY_COUNT(gSaveFile.save.data.planet)) && (used < cap); i++) {
        used += (size_t) snprintf(out + used, cap - used, "%s%02x", (i != 0) ? " " : "", raw[i]);
    }
}

void CoopSave_Enter(const uint8_t save[COOP_SAVE_SIZE]) {
    Save merged = gSaveFile.save; // this game's own, so its settings stay
    Save sent;
    u16 sentChecksum = (u16) (save[SAVE_DATA_SIZE] | (save[SAVE_DATA_SIZE + 1] << 8));
    char planets[64];
    size_t i;

    memcpy(sent.raw, save, SAVE_DATA_SIZE);
    for (i = 0; i < ARRAY_COUNT(sProgress); i++) {
        memcpy(merged.raw + sProgress[i].offset, save + sProgress[i].offset, sProgress[i].size);
    }
    // As Save_Write leaves memory, without the write.
    merged.checksum = Save_Checksum(&merged);
    gSaveFile.save = merged;
    gSaveFile.backup = merged;
    sEntered = true;

    Planets_Text(merged.raw, planets, sizeof(planets));
    CoopLog_Write(COOP_LOG_SESSION, false,
                  "save: this session plays on the host's progress, planets %s%s; this game keeps its own sound "
                  "settings, and no save is written until the session ends",
                  planets, (Save_Checksum(&sent) == sentChecksum) ? "" : " (its checksum does not match)");
}

void CoopSave_Leave(void) {
    char planets[64];

    if (!sEntered) {
        return;
    }
    sEntered = false;
    // As at boot (src/engine/fox_game.c, GSTATE_CHECK_SAVE), but nothing is written: the file is as it was.
    if (Save_Read() != 0) {
        gSaveFile.save = gDefaultSave;
        gSaveFile.backup = gDefaultSave;
    }
    Planets_Text(gSaveFile.save.raw, planets, sizeof(planets));
    CoopLog_Write(COOP_LOG_SESSION, false,
                  "save: the session is over, this game's own save is back from its file, planets %s; what the "
                  "session earned is not kept",
                  planets);
}
