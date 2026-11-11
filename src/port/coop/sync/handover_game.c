// The handover in the game, see handover_game.h.

#include "handover_game.h"

#ifdef USE_NETWORKING

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "port/coop/game/app_folder.h"
#include "port/coop/log/coop_log.h"
#include "port/coop/net/protocol.h"
#include "desync_game.h"
#include "handover.h"

#define PATH_SIZE 1024
// What SF64_COOP_TEST_HANDOVER_MS may set.
#define CAP_MAX_MS 60000
// A handover that waits looks again this often.
#define STEP_MS 1
// Session events taken in one pass: a 215 KB report is about 190 messages, so a pass or two.
#define POLL_EVENTS 512

static bool sRunning = false;
// The reason of a BEGIN that ended a running session, by peer, 0 for none: the handover that follows starts from it.
static uint8_t sHeard[COOP_HANDOVER_PEERS];

static uint32_t Cap_Ms(void) {
    const char* text = getenv("SF64_COOP_TEST_HANDOVER_MS");
    char* end;
    long value;

    if ((text == NULL) || (text[0] < '0') || (text[0] > '9')) {
        return COOP_HANDOVER_MS;
    }
    value = strtol(text, &end, 10);
    return ((*end == '\0') && (value >= 0) && (value <= CAP_MAX_MS)) ? (uint32_t) value : COOP_HANDOVER_MS;
}

static bool Handover_Send(void* user, int peer, const uint8_t* data, uint16_t size) {
    return CoopSession_Send((CoopSession*) user, peer, COOP_MSG_HANDOVER, data, size, COOP_NET_RELIABLE);
}

static bool Log_Send(void* user, int peer, const uint8_t* data, uint16_t size) {
    return CoopSession_Send((CoopSession*) user, peer, COOP_MSG_DIAG_LOG, data, size, COOP_NET_RELIABLE);
}

// A file a peer handed over, into the game's own folder under its own name: through a temporary file and a rename, so
// a half-written file never carries the name. handover.c has checked the name against the peer's player, and it is
// checked again here before it becomes part of a path. No folder is made.
static void Handover_Save(void* user, int peer, const char* name, const uint8_t* data, uint32_t size) {
    CoopSession* s = user;
    int player = CoopSession_PeerPlayer(s, peer);
    char folder[PATH_SIZE];
    char path[PATH_SIZE + COOP_HANDOVER_NAME_MAX + 2];
    char part[PATH_SIZE + COOP_HANDOVER_NAME_MAX + 8];
    FILE* f;
    bool ok;

    if (!CoopHandover_NameValid(name, strlen(name), player, CoopSession_Self(s))) {
        CoopLog_Write(COOP_LOG_SESSION, false, "handover: refused a file of player %d", player + 1);
        return;
    }
    CoopAppFolder_Path(folder, sizeof(folder));
    if (folder[0] == '\0') {
        CoopLog_Write(COOP_LOG_SESSION, false, "handover: no folder to save %s of player %d in", name, player + 1);
        return;
    }
    snprintf(path, sizeof(path), "%s/%s", folder, name);
    snprintf(part, sizeof(part), "%s.part", path);
    f = fopen(part, "wb");
    if (f == NULL) {
        CoopLog_Write(COOP_LOG_SESSION, false, "handover: could not write %s of player %d", name, player + 1);
        return;
    }
    ok = fwrite(data, 1, size, f) == size;
    ok = (fclose(f) == 0) && ok;
    // Windows' rename does not replace a file: an older copy of this report, from an earlier session, goes first.
    if (ok && (rename(part, path) != 0)) {
        remove(path);
        ok = rename(part, path) == 0;
    }
    if (!ok) {
        remove(part);
        CoopLog_Write(COOP_LOG_SESSION, false, "handover: could not write %s of player %d", name, player + 1);
        return;
    }
    CoopLog_Write(COOP_LOG_SESSION, false, "handover: saved %s of player %d, %u bytes", name, player + 1,
                  (unsigned) size);
}

// This game's report into memory, at most COOP_HANDOVER_FILE_MAX bytes. NULL if there is none to send.
static uint8_t* Report_Read(const char* path, uint32_t* size) {
    FILE* f = fopen(path, "rb");
    uint8_t* data;
    long length;

    if (f == NULL) {
        return NULL;
    }
    if ((fseek(f, 0, SEEK_END) != 0) || ((length = ftell(f)) <= 0) || (length > (long) COOP_HANDOVER_FILE_MAX) ||
        (fseek(f, 0, SEEK_SET) != 0)) {
        fclose(f);
        return NULL;
    }
    data = malloc((size_t) length);
    if ((data != NULL) && (fread(data, 1, (size_t) length, f) != (size_t) length)) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = (uint32_t) length;
    return data;
}

static const char* Base_Name(const char* path) {
    const char* slash = strrchr(path, '/');
    const char* back = strrchr(path, '\\');

    if ((back != NULL) && ((slash == NULL) || (back > slash))) {
        slash = back;
    }
    return (slash != NULL) ? slash + 1 : path;
}

static const char* Reason_Word(uint8_t reason) {
    switch (reason) {
        case COOP_HANDOVER_REASON_LEAVE:
            return "leaving";
        case COOP_HANDOVER_REASON_ANSWER:
            return "answering";
        default:
            return "the session failed";
    }
}

// The session's events while it hands over: HANDOVER messages go in, a peer that is gone is waited for no more, and
// everything else is the session's own business or dropped, as lockstep is over. At most POLL_EVENTS a pass, so a
// peer that floods cannot keep the handover from looking at the clock.
static void Handover_Poll(CoopSession* s, CoopHandover* h, uint32_t nowMs) {
    CoopSessionEvent ev;
    int events = 0;
    int i;

    while ((events++ < POLL_EVENTS) && CoopSession_Poll(s, nowMs, &ev)) {
        if ((ev.type == COOP_SESSION_EV_MESSAGE) && (ev.msgId == COOP_MSG_HANDOVER)) {
            CoopHandover_Receive(h, ev.peer, ev.data, ev.size);
        } else if (ev.type == COOP_SESSION_EV_LEFT) {
            CoopHandover_Gone(h, ev.peer);
        } else if (ev.type == COOP_SESSION_EV_ENDED) {
            CoopHandover_Gone(h, 0);
        }
    }
    for (i = 0; i < COOP_HANDOVER_PEERS; i++) {
        if (h->peers[i].open && (CoopSession_PeerPlayer(s, i) < 0)) {
            CoopHandover_Gone(h, i);
        }
    }
}

// Everything sent to every open peer is acknowledged.
static bool Handover_Acked(CoopSession* s, const CoopHandover* h) {
    int i;

    for (i = 0; i < COOP_HANDOVER_PEERS; i++) {
        CoopNetStats stats;

        if (h->peers[i].open && CoopSession_Stats(s, i, &stats) && (stats.pending != 0)) {
            return false;
        }
    }
    return true;
}

void CoopHandoverGame_Run(CoopSession* s, bool leaving) {
    CoopHandoverConfig config;
    CoopHandover h;
    char path[PATH_SIZE];
    uint8_t* report = NULL;
    uint32_t reportSize = 0;
    uint32_t start;
    uint8_t reason;
    bool answering = false;
    bool closing = false;
    bool timeUp = false;
    int peers = 0;
    int i;

    if ((s == NULL) || sRunning) {
        return;
    }
    sRunning = true;
    config.send = Handover_Send;
    config.save = Handover_Save;
    config.user = s;
    config.self = CoopSession_Self(s);
    CoopHandover_Init(&h, &config);
    for (i = 0; i < COOP_HANDOVER_PEERS; i++) {
        int player = CoopSession_PeerPlayer(s, i);

        if ((player >= 0) && (config.self >= 0)) {
            CoopHandover_AddPeer(&h, i, player);
            peers++;
            answering = answering || (sHeard[i] != 0);
        }
    }
    // Taken in any case, so that no later session hands this one's report over.
    if (CoopDesyncGame_TakeReport(path, sizeof(path)) && (peers != 0)) {
        report = Report_Read(path, &reportSize);
        if ((report == NULL) || !CoopHandover_AddFile(&h, Base_Name(path), report, reportSize)) {
            CoopLog_Write(COOP_LOG_SESSION, false, "handover: this game's report %s cannot go", Base_Name(path));
            free(report);
            report = NULL;
        }
    }
    if (peers == 0) {
        memset(sHeard, 0, sizeof(sHeard));
        sRunning = false;
        return;
    }
    reason = leaving ? COOP_HANDOVER_REASON_LEAVE : answering ? COOP_HANDOVER_REASON_ANSWER : COOP_HANDOVER_REASON_FAIL;
    start = SDL_GetTicks();
    CoopLog_Write(COOP_LOG_SESSION, false, "handover: %s, %d players still connected, sending %s", Reason_Word(reason),
                  peers, (report != NULL) ? Base_Name(path) : "no report");
    CoopHandover_Begin(&h, reason, start, Cap_Ms());
    for (i = 0; i < COOP_HANDOVER_PEERS; i++) {
        if (sHeard[i] != 0) {
            uint8_t begin[2] = { COOP_HANDOVER_BEGIN, sHeard[i] };

            CoopHandover_Receive(&h, i, begin, sizeof(begin));
        }
    }
    memset(sHeard, 0, sizeof(sHeard));

    while (true) {
        uint32_t now = SDL_GetTicks();

        Handover_Poll(s, &h, now);
        // The log first: its lines go ahead of the files in the window.
        CoopLog_Flush(now, Log_Send, s);
        CoopHandover_Pump(&h);
        if (!closing && CoopHandover_Received(&h)) {
            closing = true;
            CoopLog_Write(COOP_LOG_SESSION, false,
                          "handover: everything is in after %u ms, %u files saved, %u refused, %u messages dropped",
                          (unsigned) (now - start), (unsigned) h.stats.saved, (unsigned) h.stats.refused,
                          (unsigned) h.stats.dropped);
            CoopSession_SayClosed(s);
            CoopLog_Flush(now, Log_Send, s);
        }
        // BYE goes behind the last log line, `session closed`, on the same ordered lane.
        if (closing && !h.closing && !CoopLog_Queued()) {
            CoopHandover_Close(&h);
            CoopHandover_Pump(&h);
        }
        if (CoopHandover_Over(&h) && Handover_Acked(s, &h)) {
            break;
        }
        if (CoopHandover_TimeUp(&h, now)) {
            timeUp = true;
            break;
        }
        SDL_PumpEvents();
        SDL_Delay(STEP_MS);
    }
    CoopLog_Write(COOP_LOG_SESSION, false,
                  "handover: %s %u ms: sent %u files, saved %u, refused %u, %u duplicates, %u messages dropped",
                  timeUp ? "time up after" : "done in", (unsigned) (SDL_GetTicks() - start),
                  (unsigned) h.stats.filesOut, (unsigned) h.stats.saved, (unsigned) h.stats.refused,
                  (unsigned) h.stats.duplicates, (unsigned) h.stats.dropped);
    CoopHandover_Free(&h);
    free(report);
    sRunning = false;
}

// The clients that have joined a host's session.
static int Handover_Clients(CoopSession* s) {
    int count = 0;
    int i;

    for (i = 0; i < COOP_HANDOVER_PEERS; i++) {
        count += CoopSession_PeerPlayer(s, i) >= 0;
    }
    return count;
}

bool CoopHandoverGame_Heard(CoopSession* s, int peer, const uint8_t* data, uint16_t size, char* text, size_t cap) {
    static const uint8_t answer[3] = { COOP_HANDOVER_DONE, 0, COOP_HANDOVER_BYE };
    int player = CoopSession_PeerPlayer(s, peer);

    if (sRunning || (player < 0) || (peer >= COOP_HANDOVER_PEERS) || (size != 2) || (data[0] != COOP_HANDOVER_BEGIN)) {
        return false;
    }
    if ((CoopSession_Self(s) == 0) && (!CoopSession_LevelStarted(s) || (Handover_Clients(s) > 1))) {
        // Before the level a client that goes ends nothing for the others; in a level it ends nothing while another
        // client is there, as the session starts the level again for those who stay (session.h, "Recovery"). It waits
        // for an answer, and here is all there is: no report from this game, which plays on.
        CoopLog_Write(COOP_LOG_SESSION, false, "handover: player %d hands over and goes; the session goes on",
                      player + 1);
        CoopSession_Send(s, peer, COOP_MSG_HANDOVER, &answer[0], 2, COOP_NET_RELIABLE);
        CoopSession_Send(s, peer, COOP_MSG_HANDOVER, &answer[2], 1, COOP_NET_RELIABLE);
        return false;
    }
    sHeard[peer] = (data[1] != 0) ? data[1] : COOP_HANDOVER_REASON_FAIL;
    if (CoopSession_Self(s) != 0) {
        snprintf(text, cap, "%s",
                 (data[1] == COOP_HANDOVER_REASON_LEAVE) ? CoopSession_EndText(COOP_SESSION_END_CLOSED)
                                                         : "The host ended the session and closed the connection.");
    } else if (data[1] == COOP_HANDOVER_REASON_LEAVE) {
        // As the menu says it when the client goes without a word (menu.c, COOP_SESSION_EV_LEFT).
        snprintf(text, cap, "Player %d left, so the session is over. %s", player + 1,
                 CoopSession_EndText(COOP_SESSION_END_CLOSED));
    } else {
        snprintf(text, cap, "Player %d's game ended the session and closed the connection.", player + 1);
    }
    CoopLog_Write(COOP_LOG_SESSION, false, "handover: player %d began one (%s), so this session ends too", player + 1,
                  Reason_Word(data[1]));
    return true;
}

#else

void CoopHandoverGame_Run(CoopSession* s, bool leaving) {
    (void) s;
    (void) leaving;
}

bool CoopHandoverGame_Heard(CoopSession* s, int peer, const uint8_t* data, uint16_t size, char* text, size_t cap) {
    (void) s;
    (void) peer;
    (void) data;
    (void) size;
    (void) text;
    (void) cap;
    return false;
}

#endif
