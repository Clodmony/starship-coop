// The desync report, see desync_report.h. The layout is what tools/coop/desync_report.py reads.

#include "desync_report.h"

#include <stdio.h>
#include <string.h>

#define REPORT_VERSION 1
#define HEX_PER_LINE 64

// include/sf64level.h, LevelId 0 to 20.
static const char* sLevelNames[] = {
    "Corneria", "Meteo",    "Sector X",        "Area 6",   "level 4",  "Sector Y", "Venom",
    "Solar",    "Zoness",   "Venom (Andross)", "Training", "Macbeth",  "Titania",  "Aquas",
    "Fortuna",  "level 15", "Katina",          "Bolse",    "Sector Z", "Venom 2",  "Versus",
};

const char* CoopDesyncReport_LevelName(int32_t level, char* buffer, size_t cap) {
    if ((level >= 0) && (level < (int32_t) (sizeof(sLevelNames) / sizeof(sLevelNames[0])))) {
        return sLevelNames[level];
    }
    snprintf(buffer, cap, "level %d", (int) level);
    return buffer;
}

const char* CoopDesyncReport_ModeName(int32_t mode) {
    switch (mode) {
        case 0:
            return "rails"; // LEVELMODE_ON_RAILS
        case 1:
            return "all-range"; // LEVELMODE_ALL_RANGE
        default:
            return "other mode";
    }
}

const char* CoopDesyncReport_PlaceName(const uint8_t state[4], char* buffer, size_t cap) {
    char level[32];

    // GameState, include/sf64thread.h: 7 GSTATE_PLAY, 4 GSTATE_MAP, 8 GSTATE_ENDING
    switch (state[0]) {
        case 7:
            snprintf(buffer, cap, "%s (%s)", CoopDesyncReport_LevelName(state[1], level, sizeof(level)),
                     CoopDesyncReport_ModeName(state[2]));
            return buffer;
        case 4:
            return "the map";
        case 8:
            return "the ending";
        default:
            snprintf(buffer, cap, "game state %u", (unsigned) state[0]);
            return buffer;
    }
}

void CoopDesyncReport_Regions(const CoopDesync* d, char* out, size_t cap) {
    uint32_t bits = CoopDesync_Regions(d);
    size_t used = 0;
    int count = 0;
    int i;

    if (!d->mine.held || !d->theirs.held || (d->theirFrame != d->frame)) {
        snprintf(out, cap, "unknown");
        return;
    }
    out[0] = '\0';
    for (i = 0; i < COOP_HASH_REGION_COUNT; i++) {
        if ((bits & (1u << i)) && (used < cap)) {
            used += (size_t) snprintf(out + used, cap - used, "%s%s", (count++ == 0) ? "" : ", ",
                                      CoopStateHash_RegionName((CoopHashRegion) i));
        }
    }
    if (count == 0) {
        snprintf(out, cap, "none");
    }
}

static void Write_Hashes(FILE* f, const char* label, const CoopDesyncEntry* e) {
    int i;

    fprintf(f, "%s: %016llx", label, (unsigned long long) e->hash.total);
    for (i = 0; i < COOP_HASH_REGION_COUNT; i++) {
        fprintf(f, " %016llx", (unsigned long long) e->hash.region[i]);
    }
    fputc('\n', f);
}

static void Write_State(FILE* f, const char* label, const CoopDesyncEntry* e) {
    char name[32];

    fprintf(f, "%s: gamestate %u, level %u (%s), %s, playstate %u\n", label, e->state[0], e->state[1],
            CoopDesyncReport_LevelName(e->state[1], name, sizeof(name)), CoopDesyncReport_ModeName(e->state[2]),
            e->state[3]);
}

static void Write_Dump(FILE* f, const CoopStateDump* dump) {
    uint32_t c;

    fprintf(f, "dump: %u chunks, %u bytes, as hashed%s\n", (unsigned) dump->count, (unsigned) dump->used,
            dump->overflow ? ", cut short" : "");
    for (c = 0; c < dump->count; c++) {
        const CoopStateChunk* chunk = &dump->chunks[c];
        const uint8_t* bytes = dump->bytes + chunk->offset;
        uint32_t i;

        if ((c == 0) || (chunk->region != dump->chunks[c - 1].region)) {
            fprintf(f, "## %s\n", CoopStateHash_RegionName((CoopHashRegion) chunk->region));
        }
        if (chunk->index >= 0) {
            fprintf(f, "%s[%d] %u\n", chunk->name, (int) chunk->index, (unsigned) chunk->size);
        } else {
            fprintf(f, "%s %u\n", chunk->name, (unsigned) chunk->size);
        }
        for (i = 0; i < chunk->size; i++) {
            fprintf(f, "%02x", bytes[i]);
            if (((i % HEX_PER_LINE) == HEX_PER_LINE - 1) || (i == chunk->size - 1)) {
                fputc('\n', f);
            }
        }
    }
}

static FILE* Open_New(const char* folder, uint32_t frame, int32_t player, char* pathOut, size_t cap) {
    int n;

    for (n = 1; n <= 9; n++) {
        FILE* f;

        if (n == 1) {
            snprintf(pathOut, cap, "%s%scoop-desync-%u-player%d.txt", folder, (folder[0] != '\0') ? "/" : "",
                     (unsigned) frame, (int) player + 1);
        } else {
            snprintf(pathOut, cap, "%s%scoop-desync-%u-player%d-%d.txt", folder, (folder[0] != '\0') ? "/" : "",
                     (unsigned) frame, (int) player + 1, n);
        }
        f = fopen(pathOut, "r");
        if (f != NULL) {
            fclose(f);
            continue;
        }
        return fopen(pathOut, "w");
    }
    return NULL;
}

bool CoopDesyncReport_Write(const CoopDesync* d, const CoopStateDump* dump, const CoopDesyncReportInfo* info,
                            char* pathOut, size_t cap) {
    char regions[160];
    FILE* f;
    int i;

    if (!d->found) {
        return false;
    }
    f = Open_New(info->folder, d->frame, d->config.self, pathOut, cap);
    if (f == NULL) {
        pathOut[0] = '\0';
        return false;
    }
    CoopDesyncReport_Regions(d, regions, sizeof(regions));
    fprintf(f, "# starship-coop desync report v%d\n", REPORT_VERSION);
    fprintf(f, "# Compare two of these with: python tools/coop/desync_report.py diff <a> <b>\n");
    fprintf(f, "player: %d of %d, %s\n", (int) d->config.self + 1, (int) info->players,
            d->config.host ? "host" : "client");
    fprintf(f, "this game: %s\n", info->self);
    fprintf(f, "other game: %s\n", info->other);
    fprintf(f, "frame: %u\n", (unsigned) d->frame);
    fprintf(f, "pair: players %d and %d\n", (int) d->pair[0] + 1, (int) d->pair[1] + 1);
    fprintf(f, "seen from: player %d's hashes\n", (int) d->foundFrom + 1);
    fprintf(f, "regions differing: %s\n", regions);
    fprintf(f, "hash period: %u frames\n", (unsigned) d->config.period);
    fprintf(f, "input delay: %u frames\n", (unsigned) info->inputDelay);
    fprintf(f, "frames checked: %u, unchecked: %u, recorded: %u\n", (unsigned) d->checked, (unsigned) d->unchecked,
            (unsigned) d->frames);
    fprintf(f, "regions: total");
    for (i = 0; i < COOP_HASH_REGION_COUNT; i++) {
        fprintf(f, " %s", CoopStateHash_RegionName((CoopHashRegion) i));
    }
    fputc('\n', f);
    if (d->mine.held) {
        Write_Hashes(f, "mine", &d->mine);
        Write_State(f, "my state", &d->mine);
    } else {
        fprintf(f, "mine: not held, frame %u was %u frames back\n", (unsigned) d->frame,
                (unsigned) (d->frames - d->frame));
    }
    if (d->theirPlayer >= 0) {
        if (d->theirFrame != d->frame) {
            fprintf(f, "their frame: %u\n", (unsigned) d->theirFrame);
        }
        if (d->theirs.held) {
            Write_Hashes(f, "theirs", &d->theirs);
            Write_State(f, "their state", &d->theirs);
        } else {
            fprintf(f, "theirs: player %d no longer held frame %u\n", (int) d->theirPlayer + 1,
                    (unsigned) d->theirFrame);
        }
    } else if (d->haveTheirTotal) {
        fprintf(f, "theirs: %016llx, total only: no answer within %u ms\n", (unsigned long long) d->theirTotal,
                (unsigned) COOP_DESYNC_GRACE_MS);
    } else {
        fprintf(f, "theirs: unknown\n");
    }
    if (dump != NULL) {
        Write_Dump(f, dump);
    } else {
        fprintf(f, "dump: none\n");
    }
    fclose(f);
    return true;
}
