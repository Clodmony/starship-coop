// Identity of a player's game: wire form, comparison and the text a refused player reads. See identity.h.

#include "identity.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Most settings listed by name in one message; the rest are counted.
#define DESCRIBE_MAX_CVARS 6

void CoopIdentity_Platform(char* out, size_t cap) {
    const char* os;
    const char* cpu;
    char compiler[24];

#if defined(_WIN32)
    os = "Windows";
#elif defined(__APPLE__)
    os = "macOS";
#elif defined(__linux__)
    os = "Linux";
#else
    os = "unknown OS";
#endif

#if defined(__x86_64__) || defined(_M_X64)
    cpu = "x86-64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    cpu = "arm64";
#elif defined(__i386__) || defined(_M_IX86)
    cpu = "x86";
#else
    cpu = "unknown CPU";
#endif

#if defined(__clang__)
    snprintf(compiler, sizeof(compiler), "Clang %d.%d", __clang_major__, __clang_minor__);
#elif defined(_MSC_VER)
    snprintf(compiler, sizeof(compiler), "MSVC %d", _MSC_VER);
#elif defined(__GNUC__)
    snprintf(compiler, sizeof(compiler), "GCC %d.%d", __GNUC__, __GNUC_MINOR__);
#else
    snprintf(compiler, sizeof(compiler), "unknown compiler");
#endif

    snprintf(out, cap, "%s %s %s", os, cpu, compiler);
}

void CoopIdentity_SortRoms(CoopIdentity* id) {
    uint8_t i;

    for (i = 1; i < id->romCount; i++) {
        uint32_t v = id->roms[i];
        uint8_t j = i;

        while (j > 0 && id->roms[j - 1] > v) {
            id->roms[j] = id->roms[j - 1];
            j--;
        }
        id->roms[j] = v;
    }
}

uint32_t CoopIdentity_Compare(const CoopIdentity* a, const CoopIdentity* b) {
    uint32_t diff = 0;

    if (strcmp(a->build, b->build) != 0) {
        diff |= COOP_ID_DIFF_BUILD;
    }
    if (a->romCount != b->romCount || memcmp(a->roms, b->roms, a->romCount * sizeof(a->roms[0])) != 0) {
        diff |= COOP_ID_DIFF_ROM;
    }
    if (a->cvarTable != b->cvarTable || a->cvarCount != b->cvarCount ||
        memcmp(a->cvars, b->cvars, a->cvarCount * sizeof(a->cvars[0])) != 0) {
        diff |= COOP_ID_DIFF_CVARS;
    }
    if (strcmp(a->platform, b->platform) != 0) {
        diff |= COOP_ID_DIFF_PLATFORM;
    }
    return diff;
}

// ---- Wire form ----

typedef struct {
    uint8_t* data;
    uint16_t cap;
    uint16_t pos;
    bool ok;
} Writer;

static void W_Bytes(Writer* w, const void* src, uint16_t n) {
    if (!w->ok || w->cap - w->pos < n) {
        w->ok = false;
        return;
    }
    memcpy(w->data + w->pos, src, n);
    w->pos += n;
}

static void W_U8(Writer* w, uint8_t v) {
    W_Bytes(w, &v, 1);
}

static void W_U16(Writer* w, uint16_t v) {
    uint8_t b[2] = { (uint8_t) v, (uint8_t) (v >> 8) };

    W_Bytes(w, b, 2);
}

static void W_U32(Writer* w, uint32_t v) {
    uint8_t b[4] = { (uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24) };

    W_Bytes(w, b, 4);
}

static void W_Text(Writer* w, const char* text) {
    size_t n = 0;

    while (n < COOP_ID_TEXT - 1 && text[n] != '\0') {
        n++;
    }
    W_U8(w, (uint8_t) n);
    W_Bytes(w, text, (uint16_t) n);
}

uint16_t CoopIdentity_Write(const CoopIdentity* id, uint8_t* out, uint16_t cap) {
    Writer w = { out, cap, 0, true };
    uint16_t i;

    if (id->romCount > COOP_ID_MAX_ROMS || id->cvarCount > COOP_SIM_CVARS_MAX) {
        return 0;
    }
    W_Text(&w, id->build);
    W_Text(&w, id->platform);
    W_U8(&w, id->romCount);
    for (i = 0; i < id->romCount; i++) {
        W_U32(&w, id->roms[i]);
    }
    W_U32(&w, id->cvarTable);
    W_U16(&w, id->cvarCount);
    for (i = 0; i < id->cvarCount; i++) {
        W_U32(&w, (uint32_t) id->cvars[i]);
    }
    return w.ok ? w.pos : 0;
}

typedef struct {
    const uint8_t* data;
    uint16_t size;
    uint16_t pos;
    bool ok;
} Reader;

static const uint8_t* R_Bytes(Reader* r, uint16_t n) {
    const uint8_t* p;

    if (!r->ok || r->size - r->pos < n) {
        r->ok = false;
        return NULL;
    }
    p = r->data + r->pos;
    r->pos += n;
    return p;
}

static uint8_t R_U8(Reader* r) {
    const uint8_t* p = R_Bytes(r, 1);

    return p ? p[0] : 0;
}

static uint16_t R_U16(Reader* r) {
    const uint8_t* p = R_Bytes(r, 2);

    return p ? (uint16_t) (p[0] | (p[1] << 8)) : 0;
}

static uint32_t R_U32(Reader* r) {
    const uint8_t* p = R_Bytes(r, 4);

    return p ? (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24) : 0;
}

static void R_Text(Reader* r, char* out) {
    uint8_t n = R_U8(r);
    const uint8_t* p;
    uint8_t i;

    out[0] = '\0';
    if (n >= COOP_ID_TEXT) {
        r->ok = false;
        return;
    }
    p = R_Bytes(r, n);
    if (p == NULL) {
        return;
    }
    // Printable ASCII only: this text ends up in front of the player.
    for (i = 0; i < n; i++) {
        if (p[i] < 0x20 || p[i] > 0x7E) {
            r->ok = false;
            return;
        }
    }
    memcpy(out, p, n);
    out[n] = '\0';
}

uint16_t CoopIdentity_Read(CoopIdentity* id, const uint8_t* data, uint16_t size) {
    Reader r = { data, size, 0, true };
    uint16_t i;

    memset(id, 0, sizeof(*id));
    R_Text(&r, id->build);
    R_Text(&r, id->platform);
    id->romCount = R_U8(&r);
    if (id->romCount > COOP_ID_MAX_ROMS) {
        return 0;
    }
    for (i = 0; i < id->romCount; i++) {
        id->roms[i] = R_U32(&r);
    }
    id->cvarTable = R_U32(&r);
    id->cvarCount = R_U16(&r);
    if (id->cvarCount > COOP_SIM_CVARS_MAX) {
        return 0;
    }
    for (i = 0; i < id->cvarCount; i++) {
        id->cvars[i] = (int32_t) R_U32(&r);
    }
    return r.ok ? r.pos : 0;
}

// ---- Text for the player ----

typedef struct {
    char* out;
    size_t cap;
    size_t len;
} Text;

static void T_Add(Text* t, const char* fmt, ...) {
    va_list args;
    int n;

    if (t->cap == 0 || t->len >= t->cap - 1) {
        return;
    }
    va_start(args, fmt);
    n = vsnprintf(t->out + t->len, t->cap - t->len, fmt, args);
    va_end(args);
    if (n > 0) {
        t->len += (size_t) n;
        if (t->len > t->cap - 1) {
            t->len = t->cap - 1;
        }
    }
}

void CoopIdentity_Line(const CoopIdentity* id, char* out, size_t cap) {
    size_t used;
    uint8_t i;

    used = (size_t) snprintf(out, cap, "build %s, %s, ROM ", id->build, id->platform);
    if (id->romCount == 0 && used < cap) {
        used += (size_t) snprintf(out + used, cap - used, "none");
    }
    for (i = 0; (i < id->romCount) && (used < cap); i++) {
        const char* name = CoopIdentity_RomName(id->roms[i]);

        used += (size_t) snprintf(out + used, cap - used, "%s%s (%08X)", i ? " + " : "", name ? name : "unknown ROM",
                                  (unsigned) id->roms[i]);
    }
    if (used < cap) {
        snprintf(out + used, cap - used, ", %u sim CVars", (unsigned) id->cvarCount);
    }
}

const char* CoopIdentity_RomName(uint32_t version) {
    switch (version) {
        case 0x76924CA9: // observed in an sf64.o2r made from US 1.0, see NOTES.md WP-21
            return "US 1.0";
        case 0x94F1D5A7: // SF64_VER_US in src/port/Engine.h; which US revision is not checked
            return "US";
        case 0x6EE9ADE7:
            return "EU";
        case 0x3964945F:
            return "EU (Spanish)";
        case 0x3728D3E1:
            return "JP";
        default:
            return NULL;
    }
}

static void T_Roms(Text* t, const CoopIdentity* id) {
    uint8_t i;

    if (id->romCount == 0) {
        T_Add(t, "no ROM data");
        return;
    }
    for (i = 0; i < id->romCount; i++) {
        const char* name = CoopIdentity_RomName(id->roms[i]);

        T_Add(t, "%s%s (%08X)", i ? " + " : "", name ? name : "unknown ROM", (unsigned) id->roms[i]);
    }
}

static void T_Value(Text* t, uint16_t i, int32_t v) {
    char name[64];

    CoopSimCvars_Name(i, name, sizeof(name));
    if (strstr(name, "PathProgress") != NULL) {
        float f;

        memcpy(&f, &v, sizeof(f));
        T_Add(t, "%g", (double) f);
    } else {
        T_Add(t, "%d", (int) v);
    }
}

size_t CoopIdentity_Describe(uint32_t diff, const CoopIdentity* host, const CoopIdentity* joiner, char* out,
                             size_t cap) {
    Text t = { out, cap, 0 };

    if (cap > 0) {
        out[0] = '\0';
    }
    if (diff & COOP_ID_DIFF_BUILD) {
        T_Add(&t, "Different game builds: host %s, joining player %s. Both need the same Starship Co-op build.\n",
              host->build[0] ? host->build : "unknown", joiner->build[0] ? joiner->build : "unknown");
    }
    if (diff & COOP_ID_DIFF_ROM) {
        T_Add(&t, "Different ROMs: host ");
        T_Roms(&t, host);
        T_Add(&t, ", joining player ");
        T_Roms(&t, joiner);
        T_Add(&t, ". Both need the same ROM version.\n");
    }
    if (diff & COOP_ID_DIFF_CVARS) {
        if (host->cvarTable != joiner->cvarTable || host->cvarCount != joiner->cvarCount) {
            T_Add(&t, "Different lists of game settings; the builds differ.\n");
        } else {
            uint16_t i;
            uint32_t shown = 0;
            uint32_t more = 0;

            for (i = 0; i < host->cvarCount; i++) {
                char name[64];

                if (host->cvars[i] == joiner->cvars[i]) {
                    continue;
                }
                if (shown == DESCRIBE_MAX_CVARS) {
                    more++;
                    continue;
                }
                CoopSimCvars_Name(i, name, sizeof(name));
                T_Add(&t, "Setting \"%s\" (%s): host ", CoopSimCvars_Label(i), name);
                T_Value(&t, i, host->cvars[i]);
                T_Add(&t, ", joining player ");
                T_Value(&t, i, joiner->cvars[i]);
                T_Add(&t, ".\n");
                shown++;
            }
            if (more > 0) {
                T_Add(&t, "And %u more settings differ.\n", (unsigned) more);
            }
            T_Add(&t, "These settings change the game, so both players need the same values.\n");
        }
    }
    if (diff & COOP_ID_DIFF_PLATFORM) {
        T_Add(&t,
              "Different platforms: host %s, joining player %s. Nobody has checked that these two compute the same "
              "game; the game will say so if they part.\n",
              host->platform, joiner->platform);
    }
    // No trailing newline.
    if (t.len > 0 && out[t.len - 1] == '\n') {
        out[--t.len] = '\0';
    }
    return t.len;
}
