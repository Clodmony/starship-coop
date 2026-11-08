// The co-op log, see coop_log.h.

#include "coop_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STAMP_SIZE 24
#define PATH_SIZE 1024
#define TEXT_SIZE 384

typedef struct {
    bool open;
    uint8_t queue[COOP_LOG_QUEUE]; // entries: origin u8, length u16 little-endian, text
    uint32_t used;
    uint32_t lastSendMs;
    uint32_t droppedUnsaid; // lines dropped for a full queue, not yet said in a line
} PeerLink;

static CoopLogLevel sLevel = COOP_LOG_QUIET;
static CoopLogContext sContext;
static bool sHaveContext = false;
static bool sOpen = false;
static bool sHost = false;
static int sPlayer = -1;
static char sStamp[STAMP_SIZE];
static char sFolder[PATH_SIZE];
static FILE* sOwn = NULL;
static FILE* sFiles[COOP_LOG_PLAYERS]; // received lines, by origin player
static char sEarly[COOP_LOG_EARLY];    // a client's lines before its player index is known
static size_t sEarlyUsed = 0;
static PeerLink sPeers[COOP_LOG_PEERS];
static CoopLogStats sStats;
static bool sSaying = false; // a line about the log itself is being written: no second one from inside it
static const char* sHomeOverride = NULL;
static const char* sUserOverride = NULL;
static bool sPrivateOverride = false;

static const char* sCategoryNames[COOP_LOG_CATEGORY_COUNT] = { "session", "net",   "lockstep", "desync", "game",
                                                               "player",  "state", "warn",     "error",  "log" };

const char* CoopLog_CategoryName(CoopLogCategory category) {
    return ((category >= 0) && (category < COOP_LOG_CATEGORY_COUNT)) ? sCategoryNames[category] : "?";
}

void CoopLog_SetLevel(CoopLogLevel level) {
    sLevel = level;
}

CoopLogLevel CoopLog_Level(void) {
    return sLevel;
}

void CoopLog_SetContext(const CoopLogContext* context) {
    sHaveContext = context != NULL;
    if (sHaveContext) {
        sContext = *context;
    } else {
        memset(&sContext, 0, sizeof(sContext));
    }
}

void CoopLog_Stats(CoopLogStats* out) {
    *out = sStats;
}

// ---- Time ----

static void Clock_Now(struct tm* out, int* msOut) {
    struct timespec ts;
    time_t seconds;

    memset(&ts, 0, sizeof(ts));
    if (timespec_get(&ts, TIME_UTC) == 0) {
        ts.tv_sec = time(NULL);
        ts.tv_nsec = 0;
    }
    seconds = ts.tv_sec;
#ifdef _WIN32
    localtime_s(out, &seconds);
#else
    localtime_r(&seconds, out);
#endif
    *msOut = (int) (ts.tv_nsec / 1000000);
}

static void Clock_Line(char* out, size_t cap) {
    struct tm tm;
    int ms;

    Clock_Now(&tm, &ms);
    snprintf(out, cap, "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
             tm.tm_min, tm.tm_sec, ms);
}

static void Clock_Stamp(char* out, size_t cap) {
    struct tm tm;
    int ms;

    Clock_Now(&tm, &ms);
    snprintf(out, cap, "%04d%02d%02d-%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
             tm.tm_sec);
}

// ---- Privacy ----

void CoopLog_SetPrivate(const char* home, const char* user) {
    sPrivateOverride = (home != NULL) || (user != NULL);
    sHomeOverride = home;
    sUserOverride = user;
}

static const char* Env_First(const char* a, const char* b, const char* c) {
    const char* value = getenv(a);

    if ((value == NULL || value[0] == '\0') && b != NULL) {
        value = getenv(b);
    }
    if ((value == NULL || value[0] == '\0') && c != NULL) {
        value = getenv(c);
    }
    return ((value != NULL) && (value[0] != '\0')) ? value : NULL;
}

static const char* Private_Home(void) {
    if (sPrivateOverride) {
        return sHomeOverride;
    }
    return Env_First("HOME", "USERPROFILE", NULL);
}

static const char* Private_User(void) {
    if (sPrivateOverride) {
        return sUserOverride;
    }
    return Env_First("USER", "LOGNAME", "USERNAME");
}

static bool Slash(char c) {
    return (c == '/') || (c == '\\');
}

static bool Word(char c) {
    return ((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || (c == '_');
}

// The length of `path` at the start of `text`, slashes of either kind matching, 0 if it is not there.
static size_t Match_Path(const char* text, const char* path, size_t pathLen) {
    size_t i;

    for (i = 0; i < pathLen; i++) {
        if (text[i] == '\0') {
            return 0;
        }
        if (Slash(path[i]) ? !Slash(text[i]) : (text[i] != path[i])) {
            return 0;
        }
    }
    // A longer name that starts the same, such as the home folder of another user, stays as it is.
    return (text[i] == '\0' || Slash(text[i]) || !Word(text[i])) ? pathLen : 0;
}

static size_t Match_Word(const char* text, const char* word, size_t wordLen, bool wordBefore) {
    if (wordBefore || strncmp(text, word, wordLen) != 0) {
        return 0;
    }
    return Word(text[wordLen]) ? 0 : wordLen;
}

// 1 to 3 digits, four times with dots: an IPv4 address, not a version number with more or fewer parts.
static size_t Match_Ip(const char* text, bool wordBefore) {
    size_t i = 0;
    int part;

    if (wordBefore) {
        return 0;
    }
    for (part = 0; part < 4; part++) {
        size_t digits = 0;

        while ((digits < 3) && (text[i] >= '0') && (text[i] <= '9')) {
            i++;
            digits++;
        }
        if (digits == 0) {
            return 0;
        }
        if (part < 3) {
            if (text[i] != '.') {
                return 0;
            }
            i++;
        }
    }
    return Word(text[i]) || (text[i] == '.') ? 0 : i;
}

static void Replace_All(char* line, size_t cap, const char* what,
                        size_t (*match)(const char*, const char*, size_t, bool), const char* with) {
    char out[COOP_LOG_LINE * 2];
    size_t whatLen = (what != NULL) ? strlen(what) : 0;
    size_t in = 0;
    size_t used = 0;
    bool wordBefore = false;

    while (line[in] != '\0' && used + 1 < sizeof(out)) {
        size_t n = match(line + in, what, whatLen, wordBefore);

        if (n != 0) {
            size_t withLen = strlen(with);

            if (used + withLen + 1 >= sizeof(out)) {
                break;
            }
            memcpy(out + used, with, withLen);
            used += withLen;
            in += n;
            wordBefore = false;
        } else {
            wordBefore = Word(line[in]);
            out[used++] = line[in++];
        }
    }
    out[used] = '\0';
    snprintf(line, cap, "%s", out);
}

static size_t Match_Home(const char* text, const char* home, size_t homeLen, bool wordBefore) {
    (void) wordBefore;
    return Match_Path(text, home, homeLen);
}

static size_t Match_User(const char* text, const char* user, size_t userLen, bool wordBefore) {
    return Match_Word(text, user, userLen, wordBefore);
}

static size_t Match_Address(const char* text, const char* unused, size_t unusedLen, bool wordBefore) {
    (void) unused;
    (void) unusedLen;
    return Match_Ip(text, wordBefore);
}

void CoopLog_Scrub(char* line, size_t cap) {
    const char* home = Private_Home();
    const char* user = Private_User();
    char trimmed[PATH_SIZE];

    if (home != NULL) {
        size_t n;

        snprintf(trimmed, sizeof(trimmed), "%s", home);
        n = strlen(trimmed);
        while ((n > 0) && Slash(trimmed[n - 1])) {
            trimmed[--n] = '\0';
        }
        // A root or a one-letter folder would eat the whole line.
        if (n >= 4) {
            Replace_All(line, cap, trimmed, Match_Home, "~");
        }
    }
    if ((user != NULL) && (strlen(user) >= 2)) {
        Replace_All(line, cap, user, Match_User, "<user>");
    }
    Replace_All(line, cap, NULL, Match_Address, "<ip>");
}

// ---- Files ----

static FILE* File_Open(int player, const char* role) {
    char path[PATH_SIZE + 64];
    FILE* f;

    if (sFolder[0] == '\0') {
        return NULL;
    }
    if (player >= 0) {
        snprintf(path, sizeof(path), "%s/coop-log-%s-player%d.txt", sFolder, sStamp, player + 1);
    } else {
        snprintf(path, sizeof(path), "%s/coop-log-%s-%s.txt", sFolder, sStamp, role);
    }
    f = fopen(path, "a");
    if (f != NULL) {
        CoopLog_Scrub(path, sizeof(path));
        fprintf(f, "# coop log %s: %s, file %s\n", sStamp, role, path);
        fflush(f);
    }
    return f;
}

// A client's file waits for its player index, unless the session ends first (force).
static void Own_Open(bool force) {
    char role[32];

    if ((sOwn != NULL) || (sFolder[0] == '\0') || ((sPlayer < 0) && !force)) {
        return;
    }
    snprintf(role, sizeof(role), sHost ? "player %d, host" : "player %d, client", sPlayer + 1);
    sOwn = File_Open(sPlayer, sPlayer >= 0 ? role : "joiner");
    if ((sOwn != NULL) && (sEarlyUsed != 0)) {
        fwrite(sEarly, 1, sEarlyUsed, sOwn);
        fflush(sOwn);
    }
    sEarlyUsed = 0;
}

static void Own_Line(const char* line, size_t len) {
    if (sOwn != NULL) {
        fwrite(line, 1, len, sOwn);
        fflush(sOwn);
        sStats.written++;
    } else if (sEarlyUsed + len <= sizeof(sEarly)) {
        memcpy(sEarly + sEarlyUsed, line, len);
        sEarlyUsed += len;
        sStats.written++;
    } else {
        sStats.droppedFull++;
    }
}

static void Received_Line(int origin, const char* line, size_t len) {
    char role[48];

    if ((origin < 0) || (origin >= COOP_LOG_PLAYERS)) {
        return;
    }
    if (sFiles[origin] == NULL) {
        snprintf(role, sizeof(role), "player %d, received by player %d", origin + 1, sPlayer + 1);
        sFiles[origin] = File_Open(origin, role);
    }
    if (sFiles[origin] != NULL) {
        fwrite(line, 1, len, sFiles[origin]);
        if (len == 0 || line[len - 1] != '\n') {
            fputc('\n', sFiles[origin]);
        }
        fflush(sFiles[origin]);
    }
}

// ---- Queues ----

static uint32_t Queue_Entries(const PeerLink* p) {
    uint32_t at = 0;
    uint32_t count = 0;

    while (at + 3 <= p->used) {
        uint16_t len = (uint16_t) (p->queue[at + 1] | (p->queue[at + 2] << 8));

        at += 3 + len;
        count++;
    }
    return count;
}

static void Queue_Add(PeerLink* p, int origin, const char* text, size_t len) {
    if (!p->open) {
        return;
    }
    if (len > COOP_LOG_BATCH_TEXT) {
        len = COOP_LOG_BATCH_TEXT;
    }
    if (p->used + 3 + len > sizeof(p->queue)) {
        p->droppedUnsaid++;
        sStats.droppedFull++;
        return;
    }
    p->queue[p->used] = (uint8_t) origin;
    p->queue[p->used + 1] = (uint8_t) (len & 0xFF);
    p->queue[p->used + 2] = (uint8_t) (len >> 8);
    memcpy(p->queue + p->used + 3, text, len);
    p->used += (uint32_t) (3 + len);
}

// Takes the first entries of one origin that fit one batch: the datagram goes into out, the entries leave the queue
// only when the caller says the send worked (Queue_Drop).
static uint16_t Queue_Batch(const PeerLink* p, uint8_t* out, uint32_t* bytesOut) {
    uint32_t at = 0;
    uint16_t size = COOP_LOG_BATCH_HEADER;
    int origin = -1;

    while (at + 3 <= p->used) {
        int entryOrigin = p->queue[at];
        uint16_t len = (uint16_t) (p->queue[at + 1] | (p->queue[at + 2] << 8));

        if ((origin >= 0) && (entryOrigin != origin)) {
            break;
        }
        if (size + len > COOP_LOG_BATCH_HEADER + COOP_LOG_BATCH_TEXT) {
            break;
        }
        origin = entryOrigin;
        memcpy(out + size, p->queue + at + 3, len);
        size = (uint16_t) (size + len);
        at += 3 + len;
    }
    if (origin < 0) {
        return 0;
    }
    out[0] = (uint8_t) origin;
    out[1] = 0;
    *bytesOut = at;
    return size;
}

static void Queue_Drop(PeerLink* p, uint32_t bytes) {
    memmove(p->queue, p->queue + bytes, p->used - bytes);
    p->used -= bytes;
}

// ---- Lines ----

static void Line_Out(CoopLogCategory category, const char* text) {
    char line[COOP_LOG_LINE];
    char clock[32];
    char level[64] = "-";
    char frame[16] = "-";
    size_t len;
    int i;

    Clock_Line(clock, sizeof(clock));
    if (sHaveContext && (sContext.frame != NULL)) {
        int32_t f = sContext.frame();

        if (f >= 0) {
            snprintf(frame, sizeof(frame), "%d", (int) f);
        }
    }
    if (sHaveContext && (sContext.level != NULL)) {
        sContext.level(level, sizeof(level));
        if (level[0] == '\0') {
            snprintf(level, sizeof(level), "-");
        }
    }
    snprintf(line, sizeof(line), "%s %s %s %s %s\n", clock, frame, level, CoopLog_CategoryName(category), text);
    len = strlen(line);
    Own_Line(line, len);
    for (i = 0; i < COOP_LOG_PEERS; i++) {
        Queue_Add(&sPeers[i], sPlayer >= 0 ? sPlayer : 0, line, len);
    }
}

void CoopLog_WriteV(CoopLogCategory category, bool verboseOnly, const char* format, va_list args) {
    char text[TEXT_SIZE];
    char* p;

    if (!sOpen || (sLevel == COOP_LOG_OFF) || (verboseOnly && (sLevel != COOP_LOG_VERBOSE))) {
        return;
    }
    vsnprintf(text, sizeof(text), format, args);
    // One line stays one line, so the files parse.
    for (p = text; *p != '\0'; p++) {
        if ((*p == '\n') || (*p == '\r')) {
            *p = ' ';
        }
    }
    CoopLog_Scrub(text, sizeof(text));
    Line_Out(category, text);
}

void CoopLog_Write(CoopLogCategory category, bool verboseOnly, const char* format, ...) {
    va_list args;

    va_start(args, format);
    CoopLog_WriteV(category, verboseOnly, format, args);
    va_end(args);
}

// A line about the log itself, never from inside another one.
static void Log_Say(const char* format, ...) {
    va_list args;

    if (sSaying) {
        return;
    }
    sSaying = true;
    va_start(args, format);
    CoopLog_WriteV(COOP_LOG_LOG, false, format, args);
    va_end(args);
    sSaying = false;
}

// ---- Session ----

static void Files_Close(void) {
    int i;

    if (sOwn != NULL) {
        fclose(sOwn);
        sOwn = NULL;
    }
    for (i = 0; i < COOP_LOG_PLAYERS; i++) {
        if (sFiles[i] != NULL) {
            fclose(sFiles[i]);
            sFiles[i] = NULL;
        }
    }
}

void CoopLog_Begin(bool host, uint32_t nowMs) {
    if (sOpen) {
        CoopLog_End(nowMs, NULL, NULL);
    }
    if (sLevel == COOP_LOG_OFF) {
        return;
    }
    sFolder[0] = '\0';
    if (sHaveContext && (sContext.folder != NULL)) {
        sContext.folder(sFolder, sizeof(sFolder));
    }
    if (sFolder[0] == '\0') {
        return; // nowhere to write: tools/coop/menutest and sessiontest, which have no game folder
    }
    memset(sPeers, 0, sizeof(sPeers));
    memset(&sStats, 0, sizeof(sStats));
    sEarlyUsed = 0;
    sHost = host;
    sPlayer = host ? 0 : -1;
    Clock_Stamp(sStamp, sizeof(sStamp));
    sOpen = true;
    Own_Open(false);
    Log_Say("log opened, %s, %s", host ? "host" : "client", sLevel == COOP_LOG_VERBOSE ? "verbose" : "quiet");
}

void CoopLog_SetPlayer(int player) {
    if (!sOpen || (player < 0) || (player >= COOP_LOG_PLAYERS)) {
        return;
    }
    sPlayer = player;
    Own_Open(false);
}

bool CoopLog_IsOpen(void) {
    return sOpen;
}

void CoopLog_PeerOpen(int peer) {
    if (!sOpen || (peer < 0) || (peer >= COOP_LOG_PEERS)) {
        return;
    }
    memset(&sPeers[peer], 0, sizeof(sPeers[peer]));
    sPeers[peer].open = true;
}

static int Peer_Player(int peer) {
    return sHost ? peer + 1 : 0;
}

void CoopLog_PeerClose(int peer) {
    PeerLink* p;
    uint32_t left;

    if (!sOpen || (peer < 0) || (peer >= COOP_LOG_PEERS) || !sPeers[peer].open) {
        return;
    }
    p = &sPeers[peer];
    left = Queue_Entries(p);
    p->open = false;
    p->used = 0;
    sStats.droppedAtEnd += left;
    if ((left != 0) || (p->droppedUnsaid != 0)) {
        Log_Say(
            "streaming to player %d ended with %u lines still queued and %u dropped for a full queue; they are gone",
            Peer_Player(peer) + 1, (unsigned) left, (unsigned) p->droppedUnsaid);
    }
    p->droppedUnsaid = 0;
}

static void Peer_Pump(int peer, uint32_t nowMs, bool last, CoopLogSendFn send, CoopLogPendingFn pending, void* user) {
    PeerLink* p = &sPeers[peer];
    uint8_t batch[COOP_LOG_BATCH_HEADER + COOP_LOG_BATCH_TEXT];
    uint32_t bytes = 0;
    uint16_t size;

    if (!p->open || (p->used == 0) || (send == NULL)) {
        return;
    }
    if (!last && (nowMs - p->lastSendMs < COOP_LOG_SEND_MS)) {
        return;
    }
    if (pending != NULL) {
        uint32_t inFlight = 0;

        if (!pending(user, peer, &inFlight) || (inFlight >= COOP_LOG_MAX_PENDING)) {
            return;
        }
    }
    size = Queue_Batch(p, batch, &bytes);
    if ((size == 0) || !send(user, peer, batch, size)) {
        return;
    }
    Queue_Drop(p, bytes);
    p->lastSendMs = nowMs;
    sStats.sent++;
    if ((p->droppedUnsaid != 0) && (p->used + 256 < sizeof(p->queue))) {
        uint32_t dropped = p->droppedUnsaid;

        p->droppedUnsaid = 0;
        Log_Say("streaming to player %d dropped %u lines because its queue was full", Peer_Player(peer) + 1,
                (unsigned) dropped);
    }
}

void CoopLog_Pump(uint32_t nowMs, CoopLogSendFn send, CoopLogPendingFn pending, void* user) {
    int i;

    if (!sOpen) {
        return;
    }
    for (i = 0; i < COOP_LOG_PEERS; i++) {
        Peer_Pump(i, nowMs, false, send, pending, user);
    }
}

void CoopLog_Flush(uint32_t nowMs, CoopLogSendFn send, void* user) {
    int i;

    if (!sOpen) {
        return;
    }
    for (i = 0; i < COOP_LOG_PEERS; i++) {
        // One batch at a time while send takes them: a batch that went out counts in sStats.sent.
        while (sPeers[i].open && (sPeers[i].used != 0)) {
            uint32_t sent = sStats.sent;

            Peer_Pump(i, nowMs, true, send, NULL, user);
            if (sStats.sent == sent) {
                break;
            }
        }
    }
}

bool CoopLog_Queued(void) {
    int i;

    if (!sOpen) {
        return false;
    }
    for (i = 0; i < COOP_LOG_PEERS; i++) {
        if (sPeers[i].open && (sPeers[i].used != 0)) {
            return true;
        }
    }
    return false;
}

bool CoopLog_Receive(int peer, bool host, const uint8_t* data, uint16_t size) {
    char text[COOP_LOG_BATCH_TEXT + 1];
    int origin;
    size_t n;
    size_t start;
    int i;

    if (!sOpen) {
        return true; // nothing to write it into; a log is nobody's business to end a session over
    }
    if ((size < COOP_LOG_BATCH_HEADER + 1) || (size > COOP_LOG_BATCH_HEADER + COOP_LOG_BATCH_TEXT) || (peer < 0) ||
        (peer >= COOP_LOG_PEERS)) {
        sStats.badBatches++;
        return false;
    }
    origin = host ? peer + 1 : data[0];
    if ((origin < 0) || (origin >= COOP_LOG_PLAYERS) || (origin == sPlayer)) {
        sStats.badBatches++;
        return false;
    }
    n = (size_t) size - COOP_LOG_BATCH_HEADER;
    memcpy(text, data + COOP_LOG_BATCH_HEADER, n);
    text[n] = '\0';
    for (i = 0; i < (int) n; i++) {
        unsigned char c = (unsigned char) text[i];

        if ((c != '\n') && (c != '\t') && ((c < 0x20) || (c > 0x7E))) {
            text[i] = '?';
        }
    }
    sStats.received++;
    for (start = 0; start < n;) {
        char* end = strchr(text + start, '\n');
        size_t len = (end != NULL) ? (size_t) (end - (text + start)) + 1 : n - start;

        Received_Line(origin, text + start, len);
        if (host) {
            for (i = 0; i < COOP_LOG_PEERS; i++) {
                if (i != peer) {
                    Queue_Add(&sPeers[i], origin, text + start, len);
                    sStats.relayed += sPeers[i].open ? 1 : 0;
                }
            }
        }
        start += len;
    }
    return true;
}

void CoopLog_End(uint32_t nowMs, CoopLogSendFn send, void* user) {
    int i;

    if (!sOpen) {
        return;
    }
    // One last chance for what is queued: without a rate limit, but still one datagram per peer, as the connection is
    // about to go and nothing acknowledges it any more.
    for (i = 0; i < COOP_LOG_PEERS; i++) {
        Peer_Pump(i, nowMs, true, send, NULL, user);
    }
    for (i = 0; i < COOP_LOG_PEERS; i++) {
        CoopLog_PeerClose(i);
    }
    Own_Open(true); // a client that never learnt its index still gets its lines on disk
    Log_Say("log closed: %u lines written, %u batches sent, %u received, %u lines passed on, %u lines dropped for full "
            "queues, %u dropped at the end",
            (unsigned) sStats.written, (unsigned) sStats.sent, (unsigned) sStats.received, (unsigned) sStats.relayed,
            (unsigned) sStats.droppedFull, (unsigned) sStats.droppedAtEnd);
    Files_Close();
    sOpen = false;
    sPlayer = -1;
}
