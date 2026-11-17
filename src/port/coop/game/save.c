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

#define PLANETS ARRAY_COUNT(gSaveFile.save.data.planet)

// Save slot -> name (include/sf64level.h: PlanetId 0 to 12, then SAVE_SLOT_SOLAR, SAVE_SLOT_VENOM_1,
// SAVE_SLOT_VENOM_2).
static const char* const sPlanetNames[] = { "Meteo",   "Area 6",  "Bolse",   "Sector Z", "Sector X", "Sector Y",
                                            "Katina",  "Macbeth", "Zoness",  "Corneria", "Titania",  "Aquas",
                                            "Fortuna", "Solar",   "Venom 1", "Venom 2" };
// The order the kept planets are named in: as the map has them, from Corneria to Venom.
static const u8 sPlanetOrder[] = { PLANET_CORNERIA, PLANET_METEO,   PLANET_SECTOR_Y,   PLANET_FORTUNA,
                                   PLANET_KATINA,   PLANET_AQUAS,   PLANET_SECTOR_X,   SAVE_SLOT_SOLAR,
                                   PLANET_ZONESS,   PLANET_TITANIA, PLANET_MACBETH,    PLANET_SECTOR_Z,
                                   PLANET_BOLSE,    PLANET_AREA_6,  SAVE_SLOT_VENOM_1, SAVE_SLOT_VENOM_2 };

_Static_assert(sizeof(PlanetData) == 1 && offsetof(SaveData, planet) == 0,
               "a planet is not one byte at the save's start");
_Static_assert(ARRAY_COUNT(sPlanetNames) == PLANETS && ARRAY_COUNT(sPlanetOrder) == PLANETS,
               "a planet of the save has no name or no place in the order");
_Static_assert(PLANET_CORNERIA == 9 && PLANET_FORTUNA == 12 && SAVE_SLOT_SOLAR == 13 && SAVE_SLOT_VENOM_2 == 15,
               "the save slots are not where the names have them");

static bool sEntered = false;
// What a session earns (decision 0017, K2). The planet bytes the session began with: what the first CoopSave_Enter of
// the session put into memory. A level start that comes again in the same session (a recovery) leaves them alone.
static u8 sBegan[PLANETS];
// "Kept from this session: Corneria (medal), Meteo." after a session that added something to this game's own save,
// "" otherwise and from the next session's start on.
static char sKept[768];

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
    if (!sEntered) {
        memcpy(sBegan, merged.raw, sizeof(sBegan));
        sKept[0] = '\0';
    }
    sEntered = true;

    Planets_Text(merged.raw, planets, sizeof(planets));
    CoopLog_Write(COOP_LOG_SESSION, false,
                  "save: this session plays on the host's progress, planets %s%s; this game keeps its own sound "
                  "settings, and no save is written until the session ends",
                  planets, (Save_Checksum(&sent) == sentChecksum) ? "" : " (its checksum does not match)");
}

// The five flags of a planet byte (PlanetData, include/sf64save.h); the three bits below them are unused.
static u8 Planet_Flags(void) {
    union {
        PlanetData flags;
        u8 raw;
    } all = { 0 };

    all.flags.played = 1;
    all.flags.normalClear = 1;
    all.flags.normalMedal = 1;
    all.flags.expertClear = 1;
    all.flags.expertMedal = 1;
    return all.raw;
}

// "Corneria (medal), Meteo" for the flags of `kept`, one byte per planet; "" for none. A planet stands alone when its
// normal clear was kept, and says what else or what instead: played (flown, not cleared), expert, medal, expert medal.
static void Kept_Text(const u8* kept, char* out, size_t cap) {
    size_t used = 0;
    s32 i;

    out[0] = '\0';
    for (i = 0; (i < PLANETS) && (used < cap); i++) {
        s32 slot = sPlanetOrder[i];
        union {
            u8 raw;
            PlanetData flags;
        } planet;
        const char* words[4];
        s32 count = 0;
        s32 w;

        planet.raw = kept[slot];
        if (planet.raw == 0) {
            continue;
        }
        if (planet.flags.played && !planet.flags.normalClear && !planet.flags.expertClear) {
            words[count++] = "played";
        }
        if (planet.flags.expertClear) {
            words[count++] = "expert";
        }
        if (planet.flags.normalMedal) {
            words[count++] = "medal";
        }
        if (planet.flags.expertMedal) {
            words[count++] = "expert medal";
        }
        used += (size_t) snprintf(out + used, cap - used, "%s%s", (used != 0) ? ", " : "", sPlanetNames[slot]);
        for (w = 0; (w < count) && (used < cap); w++) {
            used += (size_t) snprintf(out + used, cap - used, "%s%s%s", (w == 0) ? " (" : ", ", words[w],
                                      (w == count - 1) ? ")" : "");
        }
    }
}

const char* CoopSave_KeptLine(void) {
    return sKept;
}

void CoopSave_Leave(void) {
    Save session;
    u8 kept[PLANETS];
    u8 flags = Planet_Flags();
    char planets[64];
    char names[sizeof(sKept) - 32];
    bool any = false;
    s32 i;

    if (!sEntered) {
        return;
    }
    sEntered = false;
    sKept[0] = '\0';
    session = gSaveFile.save; // the session's copy as the session leaves it
    // As at boot (src/engine/fox_game.c, GSTATE_CHECK_SAVE), but a file that cannot be read is left alone: nothing is
    // written over it, so nothing of the session is kept.
    if (Save_Read() != 0) {
        gSaveFile.save = gDefaultSave;
        gSaveFile.backup = gDefaultSave;
        CoopLog_Write(COOP_LOG_SESSION, false,
                      "save: the session is over, and this game's own save file could not be read: nothing is "
                      "written, and what the session earned is not kept");
        return;
    }
    Planets_Text(gSaveFile.save.raw, planets, sizeof(planets));
    // What the session earned (decision 0017, K2): the flags its copy holds now and did not begin with, so not what
    // the host had before. Of those, the ones this game's own save does not have yet are kept: an OR, which cannot
    // take a flag away. The ranking table, the ending-seen byte and everything else of the session's copy are dropped.
    for (i = 0; i < PLANETS; i++) {
        kept[i] = (u8) (session.raw[i] & ~sBegan[i] & ~gSaveFile.save.raw[i] & flags);
        any = any || (kept[i] != 0);
    }
    if (!any) {
        CoopLog_Write(COOP_LOG_SESSION, false,
                      "save: the session is over, this game's own save is back from its file, planets %s; the "
                      "session earned nothing this save does not have, so nothing is written",
                      planets);
        return;
    }
    for (i = 0; i < PLANETS; i++) {
        gSaveFile.save.raw[i] |= kept[i];
    }
    Kept_Text(kept, names, sizeof(names));
    // The one real write of a session, after it: the game's own Save_Write, with its checksum and its backup copy.
    // It returns Save_WriteData's answer, true when the file was written (src/sys/sys_joybus.c).
    if (!Save_Write()) {
        CoopLog_Write(COOP_LOG_SESSION, false,
                      "save: the session is over, this game's own save is back from its file, planets %s; the save "
                      "file could not be written, so what the session earned is not kept: %s",
                      planets, names);
        return;
    }
    snprintf(sKept, sizeof(sKept), "Kept from this session: %s.", names);
    Planets_Text(gSaveFile.save.raw, planets, sizeof(planets));
    CoopLog_Write(COOP_LOG_SESSION, false,
                  "save: the session is over, this game's own save is back from its file and keeps what the session "
                  "earned: %s; planets now %s, written to the save file",
                  names, planets);
}
