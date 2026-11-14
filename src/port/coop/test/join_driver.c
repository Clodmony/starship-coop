// A scripted join flow for tests (WP-24). See join_driver.h for how to switch it on.
//
// The session runs on GamePreUpdateEvent, before any game logic of the frame, so CoopLevelStart_Enter's change of level
// is taken up by Game_SetGameState on the same frame.

#ifdef USE_NETWORKING

#include "join_driver.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "global.h"
#include "port/coop/game/level_start.h"
#include "port/coop/game/names.h"
#include "port/coop/game/players.h"
#include "port/coop/game/save.h"
#include "port/coop/session/identity_game.h"
#include "port/coop/net/protocol.h"
#include "port/coop/session/session.h"
#include "port/coop/session/session_id.h"
#include "port/coop/sync/handover_game.h"
#include "port/coop/sync/lockstep_game.h"
#include "port/coop/test/switches.h"
#include "port/hooks/Events.h"

// src/sys/sys_math.c
extern s32 sRandSeed1;
extern s32 sRandSeed2;
extern s32 sRandSeed3;
extern s32 sSeededRandSeed1;
extern s32 sSeededRandSeed2;
extern s32 sSeededRandSeed3;

#define JOIN_MAX_PICKS 4
// A game that is loading a level on a busy machine stalls for a while, and that is no reason to lose a test.
#define JOIN_TIMEOUT_MS 30000

static const char* sPortFile = NULL;
static const char* sConnect = NULL;
static FILE* sReport = NULL;
static CoopSession* sSession = NULL;
static bool sTried = false;
static bool sFrameStarted = false;
static u32 sFrame = 0;
static u8 sPicks[JOIN_MAX_PICKS];
static s32 sPickCount = 0;
static s32 sNextPick = 0;
static s32 sClients = 1;
static s32 sJoined = 0;
static s32 sLevel = 0;
static s32 sPhase = 0;
static s32 sCheckpoint = 0; // SF64_COOP_JOIN_CHECKPOINT (#282)
static u32 sStartFrame = 0;
static u32 sConnectFrame = 0;
static u32 sConnectTitle = 0; // frames in a row at the title screen before the session opens
static u32 sTitleRun = 0;
static bool sPokeTeam = false;
static bool sKeepOwn = false;
static bool sKeepOwnSave = false;
static const char* sLeaveFile = NULL;
static bool sWatch = false;
static bool sStartTaken = false;
static bool sStartRefusedSaid = false;
static bool sEntered = false;
static bool sRunningSaid = false;
static bool sLeaving = false; // a level start was taken, and the game has not left where it was yet
static s32 sSelf = 0;
static const char* sName = NULL;
static char sNamesSaid[COOP_SESSION_PLAYERS * (COOP_NAME_MAX + 1) + 1];
static u8 sCharacter = 0;       // what this client flew at the last level start, to come back as
static bool sRejoining = false; // this session is the one that goes back to the host
static char sHost[256];         // a client's host and port, to come back to
static u16 sHostPort = 0;

static void Report(const char* format, ...) {
    va_list args;

    if (sReport == NULL) {
        return;
    }
    va_start(args, format);
    vfprintf(sReport, format, args);
    va_end(args);
    fputc('\n', sReport);
    fflush(sReport);
}

static const char* Character_Word(int32_t character) {
    const char* name = CoopSession_CharacterName((u8) character);

    return (name[0] != '\0') ? name : "-";
}

static void Picks_Parse(const char* text) {
    static const struct {
        const char* name;
        u8 character;
    } names[] = { { "falco", COOP_CHARACTER_FALCO },
                  { "slippy", COOP_CHARACTER_SLIPPY },
                  { "peppy", COOP_CHARACTER_PEPPY } };
    char copy[64];
    char* word;
    size_t i;

    snprintf(copy, sizeof(copy), "%s", text);
    for (word = strtok(copy, ","); (word != NULL) && (sPickCount < JOIN_MAX_PICKS); word = strtok(NULL, ",")) {
        for (i = 0; i < ARRAY_COUNT(names); i++) {
            if (strcmp(word, names[i].name) == 0) {
                sPicks[sPickCount++] = names[i].character;
            }
        }
    }
}

static void Pick_Next(void) {
    if (sNextPick < sPickCount) {
        Report("pick character=%s", Character_Word(sPicks[sNextPick]));
        CoopSession_Pick(sSession, (CoopCharacter) sPicks[sNextPick++]);
    }
}

static const char* Env_Text(const char* name) {
    const char* value = getenv(name);

    return ((value != NULL) && (value[0] != '\0')) ? value : NULL;
}

static void Session_Open(void) {
    CoopNetConfig config = { 0 };
    CoopIdentity id;
    CoopTransport* t;

    sTried = true;
    config.timeoutMs = JOIN_TIMEOUT_MS;
    CoopIdentity_FromGame(&id);
    if (sPortFile != NULL) {
        FILE* f;

        t = CoopNetSdl_Host(0, &config);
        if (t == NULL) {
            Report("ended text=could not host");
            return;
        }
        f = fopen(sPortFile, "w");
        if (f != NULL) {
            fprintf(f, "%u\n", (unsigned) CoopNetSdl_Port(t));
            fclose(f);
        }
        sSession = CoopSession_Host(t, &id);
        // SF64_COOP_JOIN_NO_ID: a session nobody can come back to, as before the session id.
        if ((sSession != NULL) && (Env_Text("SF64_COOP_JOIN_NO_ID") == NULL)) {
            CoopSession_SetId(sSession, CoopSessionId_New());
        }
    } else {
        char host[256];
        const char* colon = strrchr(sConnect, ':');
        size_t n = (colon != NULL) ? (size_t) (colon - sConnect) : 0;

        if ((colon == NULL) || (n >= sizeof(host))) {
            Report("ended text=bad address");
            return;
        }
        memcpy(host, sConnect, n);
        host[n] = '\0';
        snprintf(sHost, sizeof(sHost), "%s", host);
        sHostPort = (uint16_t) atoi(colon + 1);
        t = CoopNetSdl_Connect(host, (uint16_t) atoi(colon + 1), &config);
        if (t == NULL) {
            Report("ended text=could not connect");
            return;
        }
        sSession = CoopSession_Join(t, &id, SDL_GetTicks());
    }
    if ((sSession != NULL) && (sName != NULL)) {
        CoopSession_SetName(sSession, sName);
    }
    Report("session build=%s", id.build);
}

// The players' names as the session holds them, into the game as the menu hands them in (#258), and a `names` line
// whenever they change: one word per player slot, - for none, a space as _.
static void Names_Pump(void) {
    const CoopSessionNames* names = (sSession != NULL) ? CoopSession_Names(sSession) : NULL;
    char said[sizeof(sNamesSaid)];
    size_t used = 0;
    s32 i;
    const char* c;

    for (i = 0; i < COOP_SESSION_PLAYERS; i++) {
        const char* name = (names != NULL) ? names->name[i] : "";

        CoopNames_Set(i, name);
        if (i != 0) {
            said[used++] = ',';
        }
        if (name[0] == '\0') {
            said[used++] = '-';
        }
        for (c = name; *c != '\0'; c++) {
            said[used++] = (*c == ' ') ? '_' : *c;
        }
    }
    said[used] = '\0';
    if (strcmp(said, sNamesSaid) != 0) {
        memcpy(sNamesSaid, said, used + 1);
        Report("names frame=%u names=%s", (unsigned) sFrame, said);
    }
}

static void Values_Word(char* out, size_t size, const int32_t* values, s32 count) {
    size_t used = 0;
    s32 i;

    out[0] = '\0';
    for (i = 0; (i < count) && (used < size); i++) {
        used += (size_t) snprintf(out + used, size - used, "%s%d", (i != 0) ? "," : "", (int) values[i]);
    }
}

static void Report_LevelStart(const CoopLevelStart* start) {
    static char values[COOP_LEVEL_START_VALUES * 12];

    Values_Word(values, sizeof(values), start->values, COOP_LEVEL_START_VALUES);
    Report("level_start frame=%u level=%u phase=%u run=%u roster=%s,%s,%s,%s values=%s", (unsigned) sFrame,
           start->level, start->phase, start->run, Character_Word(start->roster[0]), Character_Word(start->roster[1]),
           Character_Word(start->roster[2]), Character_Word(start->roster[3]), values);
}

static bool Link_Send(int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane) {
    return (sSession != NULL) && CoopSession_Send(sSession, peer, msgId, data, size, lane);
}

// This game's save in memory, all 256 bytes as the file holds them, for `save_session` and `save_after`.
static void Report_Save(const char* word) {
    char hex[2 * COOP_SAVE_SIZE + 1];
    const u8* raw = (const u8*) &gSaveFile.save;
    s32 i;

    for (i = 0; i < COOP_SAVE_SIZE; i++) {
        snprintf(hex + 2 * i, 3, "%02x", raw[i]);
    }
    Report("%s frame=%u save=%s", word, (unsigned) sFrame, hex);
}

static void Session_Pump(void);

// As the menu ends one (menu_game.c): lockstep first, then the handover while the others can still be reached (#189).
// leaving: this player chose to go, as Leave in the menu.
static void Session_EndAs(bool leaving) {
    CoopSyncGame_End();
    if (sEntered) {
        Report_Save("save_after"); // this game's own again (CoopSave_Leave)
    }
    CoopHandoverGame_Run(sSession, leaving);
    CoopSession_Close(sSession);
    sSession = NULL;
    if (sName != NULL) {
        Names_Pump(); // none from here on
    }
}

static void Session_End(void) {
    Session_EndAs(false);
}

// SF64_COOP_JOIN_LEAVE_FILE: once that file is there, this game leaves its session and plays on.
static void Leave_Check(void) {
    FILE* f;

    if ((sLeaveFile == NULL) || !sEntered || (sSession == NULL)) {
        return;
    }
    f = fopen(sLeaveFile, "r");
    if (f == NULL) {
        return;
    }
    fclose(f);
    Report("leaving frame=%u", (unsigned) sFrame);
    Session_EndAs(true);
}

static void Link_Fail(const char* text) {
    Report("ended text=%s", text);
    Session_End();
}

// For a desync report: this game's identity, or a peer's as the session holds it.
static void Link_Identity(int peer, char* out, size_t cap) {
    const CoopIdentity* id = NULL;
    CoopIdentity own;

    out[0] = '\0';
    if (peer < 0) {
        CoopIdentity_FromGame(&own);
        id = &own;
    } else if (sSession != NULL) {
        id = CoopSession_PeerIdentity(sSession, peer);
    }
    if (id != NULL) {
        CoopIdentity_Line(id, out, cap);
    }
}

// As the menu has it (menu_game.c): a desync, or on the host a player who sends no input, starts the level again where
// the session can do that. `parted` keeps what a failure would have said.
static bool Link_Recover(int32_t reason, int32_t player, const char* text) {
    uint32_t now = SDL_GetTicks();
    bool taken;

    if (sSession == NULL) {
        return false;
    }
    if (reason == COOP_SYNC_RECOVER_STALL) {
        taken = (sPortFile != NULL) && CoopSession_Kick(sSession, player - 1, now);
    } else {
        taken = CoopSession_Recover(sSession, COOP_RECOVER_DESYNC, now);
    }
    if (taken) {
        Report("parted frame=%u reason=%d text=%s", (unsigned) sFrame, (int) reason, text);
    }
    return taken;
}

static void Link_Entering(const CoopLevelStart* start) {
    if (sSession != NULL) {
        Report("entering frame=%u level=%u phase=%u", (unsigned) sFrame, start->level, start->phase);
        CoopSession_SetRestart(sSession, start);
    }
}

static const CoopSyncLink sLink = { Link_Send, Session_Pump, Link_Fail, Link_Identity, Link_Recover, Link_Entering };

// As the menu has it (menu.c, Menu_Rejoin): the connection to the host went in a level, without the host saying that
// the session is over, so this game goes back on a new connection as the player it was. True if that is under way.
static bool Session_Rejoin(CoopSessionEnd end) {
    CoopNetConfig config = { 0 };
    CoopIdentity id;
    CoopTransport* t;
    uint64_t sessionId;

    if ((sPortFile != NULL) || !sEntered || sRejoining || (sCharacter == 0) ||
        ((end != COOP_SESSION_END_TIMEOUT) && (end != COOP_SESSION_END_CLOSED))) {
        return false;
    }
    sessionId = CoopSession_Id(sSession);
    if (sessionId == 0) {
        return false;
    }
    config.timeoutMs = JOIN_TIMEOUT_MS;
    t = CoopNetSdl_Connect(sHost, sHostPort, &config);
    if (t == NULL) {
        return false;
    }
    CoopSession_Close(sSession);
    CoopIdentity_FromGame(&id);
    sSession = CoopSession_Rejoin(t, &id, SDL_GetTicks(), sessionId, sCharacter);
    sRejoining = true;
    CoopSyncGame_Pause();
    Report("rejoin frame=%u character=%s", (unsigned) sFrame, Character_Word(sCharacter));
    return sSession != NULL;
}

// The control: everything Enter does but the values and the save, so the two games go to one level from what they
// each brought.
static void Enter_KeepOwn(const CoopLevelStart* start) {
    CoopLevelStart own;

    CoopLevelStart_FromGame(&own, start->level, start->phase);
    memcpy(own.roster, start->roster, sizeof(own.roster));
    CoopSave_Enter(own.save);
    CoopLevelStart_Enter(&own);
}

// The session's save as the host sent it, or with SF64_COOP_JOIN_KEEP_OWN_SAVE this game's own: the apply switched
// off, the control for the save (decision 0009 item 8).
static void Enter_Save(const CoopLevelStart* start) {
    uint8_t own[COOP_SAVE_SIZE];

    if (sKeepOwnSave) {
        CoopSave_FromGame(own);
        CoopSave_Enter(own);
    } else {
        CoopSave_Enter(start->save);
    }
}

static void Session_Event(const CoopSessionEvent* ev) {
    switch (ev->type) {
        case COOP_SESSION_EV_JOINED:
            if (sPortFile != NULL) {
                sJoined++;
            } else {
                Report("joined player=%u rejoined=%d", ev->playerIndex, (int) ev->rejoined);
                sSelf = ev->playerIndex;
                if (!ev->rejoined) {
                    Pick_Next();
                }
            }
            break;
        case COOP_SESSION_EV_REFUSED:
            Report("refused text=%s", ev->text);
            break;
        case COOP_SESSION_EV_LEFT:
            sJoined--;
            Report("left away=%d text=%s", (int) ev->away, ev->text);
            // A level needs every player, as the menu has it: the session starts it again for those who stay, and
            // with nobody left it is over.
            if (sEntered && !CoopSession_Recovering(sSession)) {
                Link_Fail(ev->text);
            }
            break;
        case COOP_SESSION_EV_RECOVER:
            Report("recover frame=%u reason=%d player=%d text=%s", (unsigned) sFrame, (int) ev->reason,
                   (int) ev->playerIndex, ev->text);
            // As the menu has it (CoopMenuGame_Recover): the host's checkpoint, if it reached one in the level.
            if ((sPortFile != NULL) && (CoopSyncGame_Frame() >= 0)) {
                CoopLevelStart checkpoint;

                if (CoopLevelStart_AtCheckpoint(&checkpoint)) {
                    Report("restart_at_checkpoint frame=%u level=%u", (unsigned) sFrame, checkpoint.level);
                    CoopSession_SetRestart(sSession, &checkpoint);
                }
            }
            CoopSyncGame_Pause();
            break;
        case COOP_SESSION_EV_ENDED:
            if (Session_Rejoin(ev->end)) {
                break;
            }
            Report("ended text=%s", ev->text);
            if (sEntered) {
                Session_End();
            }
            break;
        case COOP_SESSION_EV_MESSAGE:
            if (ev->msgId == COOP_MSG_HANDOVER) {
                char text[COOP_SESSION_TEXT];

                if (CoopHandoverGame_Heard(sSession, ev->peer, ev->data, ev->size, text, sizeof(text))) {
                    Link_Fail(text);
                }
                break;
            }
            CoopSyncGame_Receive(ev->peer, ev->msgId, ev->data, ev->size);
            break;
        case COOP_SESSION_EV_ROSTER: {
            const CoopRoster* r = CoopSession_Roster(sSession);

            Report("roster %s %s %s %s", Character_Word(r->character[0]), Character_Word(r->character[1]),
                   Character_Word(r->character[2]), Character_Word(r->character[3]));
            break;
        }
        case COOP_SESSION_EV_PICK_REFUSED:
            Report("pick_refused character=%s", Character_Word(ev->character));
            if (sPortFile == NULL) {
                Pick_Next();
            }
            break;
        case COOP_SESSION_EV_LEVEL_START:
            Report_LevelStart(ev->levelStart);
            // Here and not earlier: the title screen sets it on every frame (src/overlays/ovl_menu/fox_title.c:171).
            if (Env_Text("SF64_COOP_JOIN_MAIN_PORT") != NULL) {
                gMainController = atoi(Env_Text("SF64_COOP_JOIN_MAIN_PORT"));
            }
            if (sKeepOwn) {
                Enter_KeepOwn(ev->levelStart);
            } else {
                Enter_Save(ev->levelStart);
                CoopLevelStart_Enter(ev->levelStart);
            }
            Report_Save("save_session");
            sEntered = true;
            sRunningSaid = false; // `level_running` once per level start: a test finds each run's first frame by it
            sLeaving = true;
            sRejoining = false;
            sCharacter = ev->levelStart->roster[sSelf];
            CoopSyncGame_Begin(&sLink, ev->levelStart->roster, sSelf, sPortFile != NULL, ev->levelStart->run);
            break;
        default:
            break;
    }
}

// Values a fresh game does not have, in what no level start clears. Every game enters from the host's, so this must
// not show in the hash of a level; without the values on the wire it does.
static void Team_Poke(void) {
    s32 i;

    for (i = TEAM_ID_FALCO; i <= TEAM_ID_PEPPY; i++) {
        gTeamDamage[i] = 40 + i;
    }
    for (i = 0; i < 4; i++) {
        gTeamEventActorIndex[i] = 10 + i;
    }
}

static void Host_TryStart(void) {
    CoopLevelStart start;

    if ((sPortFile == NULL) || sStartTaken || (sJoined < sClients) || (sFrame < sStartFrame)) {
        return;
    }
    if (sPokeTeam) {
        Team_Poke();
    }
    CoopLevelStart_FromGame(&start, (u8) sLevel, (u8) sPhase);
    if (sCheckpoint > 0) {
        CoopLevelStart_SetAllRangeCheckpoint(&start, sCheckpoint);
    }
    CoopTestSwitches_StartSeeds(start.values);
    sStartTaken = CoopSession_StartLevel(sSession, &start);
    if (!sStartTaken && !sStartRefusedSaid) {
        sStartRefusedSaid = true;
        Report("start_refused frame=%u", (unsigned) sFrame);
    }
}

static void Report_Running(void) {
    int32_t seeds[6];
    char word[128];
    char damage[96];
    char event[64];

    // A level start that came in a level (the session started it again): the game is still in the old one on the frame
    // of the event, and leaves PLAY_UPDATE on its way into the new one.
    if (sLeaving && ((gGameState != GSTATE_PLAY) || (gPlayState != PLAY_UPDATE))) {
        sLeaving = false;
    }
    if (!sEntered || sRunningSaid || sLeaving || (gGameState != GSTATE_PLAY) || (gPlayState != PLAY_UPDATE)) {
        return;
    }
    sRunningSaid = true;
    seeds[0] = sRandSeed1;
    seeds[1] = sRandSeed2;
    seeds[2] = sRandSeed3;
    seeds[3] = sSeededRandSeed1;
    seeds[4] = sSeededRandSeed2;
    seeds[5] = sSeededRandSeed3;
    Values_Word(word, sizeof(word), seeds, 6);
    Values_Word(damage, sizeof(damage), gTeamDamage, ARRAY_COUNT(gTeamDamage));
    Values_Word(event, sizeof(event), gTeamEventActorIndex, ARRAY_COUNT(gTeamEventActorIndex));
    Report("level_running frame=%u level=%d phase=%d players=%d characters=%s,%s,%s,%s seeds=%s team_damage=%s "
           "team_event=%s main=%d",
           (unsigned) sFrame, (int) gCurrentLevel, (int) gLevelPhase, (int) CoopPlayers_Count(),
           Character_Word(CoopPlayers_Character(0)), Character_Word(CoopPlayers_Character(1)),
           Character_Word(CoopPlayers_Character(2)), Character_Word(CoopPlayers_Character(3)), word, damage, event,
           (int) gMainController);
}

static void Session_Pump(void) {
    CoopSessionEvent ev;

    while ((sSession != NULL) && CoopSession_Poll(sSession, SDL_GetTicks(), &ev)) {
        Session_Event(&ev);
    }
    if ((sSession != NULL) && (sName != NULL)) {
        Names_Pump();
    }
}

static void CoopJoinDriver_OnGamePreUpdate(IEvent* event) {
    (void) event;
    if (!sFrameStarted) {
        if (gGameState != GSTATE_START) {
            return;
        }
        sFrameStarted = true;
    } else {
        sFrame++;
    }
    if (sWatch) {
        // A roster is there from the level start on, and nothing but a session sets one.
        sEntered = CoopPlayers_Character(0) != 0;
        Report_Running();
        return;
    }
    sTitleRun = (gGameState == GSTATE_TITLE) ? sTitleRun + 1 : 0;
    if (!sTried && (sFrame >= sConnectFrame) && (sTitleRun >= sConnectTitle)) {
        Session_Open();
    }
    Report_Running();
    if (sSession == NULL) {
        return;
    }
    Session_Pump();
    if (sSession != NULL) {
        Host_TryStart();
    }
    Leave_Check();
    Report_Running();
}

void CoopJoinDriver_Init(void) {
    const char* report = Env_Text("SF64_COOP_JOIN_REPORT");
    const char* picks = Env_Text("SF64_COOP_JOIN_PICKS");
    const char* clients = Env_Text("SF64_COOP_JOIN_CLIENTS");
    const char* level = Env_Text("SF64_COOP_JOIN_LEVEL");
    const char* startFrame = Env_Text("SF64_COOP_JOIN_START_FRAME");
    const char* connectFrame = Env_Text("SF64_COOP_JOIN_CONNECT_FRAME");
    const char* connectTitle = Env_Text("SF64_COOP_JOIN_CONNECT_TITLE");

    sPortFile = Env_Text("SF64_COOP_JOIN_PORT_FILE");
    sConnect = Env_Text("SF64_COOP_JOIN_CONNECT");
    sWatch = Env_Text("SF64_COOP_JOIN_WATCH") != NULL;
    if ((sPortFile == NULL) && (sConnect == NULL) && !sWatch) {
        return;
    }
    if (report != NULL) {
        sReport = fopen(report, "w");
    }
    if (picks != NULL) {
        Picks_Parse(picks);
    }
    sClients = (clients != NULL) ? atoi(clients) : 1;
    sLevel = (level != NULL) ? atoi(level) : LEVEL_CORNERIA;
    sPhase = (Env_Text("SF64_COOP_JOIN_PHASE") != NULL) ? atoi(Env_Text("SF64_COOP_JOIN_PHASE")) : 0;
    sCheckpoint = (Env_Text("SF64_COOP_JOIN_CHECKPOINT") != NULL) ? atoi(Env_Text("SF64_COOP_JOIN_CHECKPOINT")) : 0;
    sStartFrame = (startFrame != NULL) ? (u32) atoi(startFrame) : 0;
    sConnectFrame = (connectFrame != NULL) ? (u32) atoi(connectFrame) : 0;
    sConnectTitle = (connectTitle != NULL) ? (u32) atoi(connectTitle) : 0;
    sPokeTeam = Env_Text("SF64_COOP_JOIN_POKE_TEAM") != NULL;
    sKeepOwn = Env_Text("SF64_COOP_JOIN_KEEP_OWN") != NULL;
    sKeepOwnSave = Env_Text("SF64_COOP_JOIN_KEEP_OWN_SAVE") != NULL;
    sLeaveFile = Env_Text("SF64_COOP_JOIN_LEAVE_FILE");
    sName = Env_Text("SF64_COOP_JOIN_NAME");
    if (sName != NULL) {
        strcpy(sNamesSaid, "-,-,-,-");
    }
    // After the state hash (EVENT_PRIORITY_NORMAL): a frame's hash shows what came before this frame's session.
    REGISTER_LISTENER(GamePreUpdateEvent, CoopJoinDriver_OnGamePreUpdate, EVENT_PRIORITY_HIGH);
}

#else

#include "join_driver.h"

void CoopJoinDriver_Init(void) {
}

#endif
