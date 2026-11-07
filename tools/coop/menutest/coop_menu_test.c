// Test program for the co-op menu (WP-25). tools/coop/menu_test.py drives it; see docs/coop/QA.md.
//
//   coop_menu_test sim
//       The menu layer of src/port/coop/ui/menu.c on the real session and the real transport core, in a virtual
//       network with a fake clock: no sockets, no window, no game. Prints one line, exits 0 on PASS.
//   coop_menu_test udp
//       One host and two clients in this process, over real UDP on loopback, through the same transport the game uses,
//       up to the level start. Prints one line, exits 0 on PASS.

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/coop/net/net_core.h"
#include "port/coop/net/protocol.h"
#include "port/coop/net/transport.h"
#include "port/coop/session/identity.h"
#include "port/coop/session/session.h"
#include "port/coop/session/sim_cvars.h"
#include "port/coop/ui/menu.h"

#define ROM_US10 0x76924CA9u

#define NO_PLAYER COOP_CHARACTER_NO_PLAYER
#define UNPICKED COOP_CHARACTER_UNPICKED
#define FOX COOP_CHARACTER_FOX
#define FALCO COOP_CHARACTER_FALCO
#define SLIPPY COOP_CHARACTER_SLIPPY
#define PEPPY COOP_CHARACTER_PEPPY

static const char* sScenario = "?";

static void Fail(const char* what) {
    printf("FAIL %s: %s\n", sScenario, what);
    exit(1);
}

static void FailText(const char* what, const char* text) {
    printf("FAIL %s: %s, got \"%s\"\n", sScenario, what, text);
    exit(1);
}

#define EXPECT(cond, what) \
    do {                   \
        if (!(cond)) {     \
            Fail(what);    \
        }                  \
    } while (0)

static void Expect_Has(const char* text, const char* needle, const char* what) {
    if (strstr(text, needle) == NULL) {
        FailText(what, text);
    }
}

static void Expect_Lacks(const char* text, const char* needle, const char* what) {
    if (strstr(text, needle) != NULL) {
        FailText(what, text);
    }
}

// ---- Identities ----

static int32_t Default_Int(const char* name, int32_t defaultValue) {
    (void) name;
    return defaultValue;
}

static float Default_Float(const char* name, float defaultValue) {
    (void) name;
    return defaultValue;
}

static const char* sBuild = "test-build";
static const char* sPlatform = NULL; // this machine's
static int sIdentityReads;

static void Id_Test(CoopIdentity* id) {
    memset(id, 0, sizeof(*id));
    snprintf(id->build, sizeof(id->build), "%s", sBuild);
    CoopIdentity_Platform(id->platform, sizeof(id->platform));
    if (sPlatform != NULL) {
        snprintf(id->platform, sizeof(id->platform), "%s", sPlatform);
    }
    id->romCount = 1;
    id->roms[0] = ROM_US10;
    id->cvarTable = CoopSimCvars_TableHash();
    id->cvarCount = CoopSimCvars_Count();
    CoopSimCvars_Read(id->cvars, Default_Int, Default_Float);
    sIdentityReads++;
}

// ---- The game, as far as the menu sees it ----

// The host and three clients. A fifth menu is for a player the host has no room for.
#define MENUS 5
#define NOT_TICKING (-1)

#define TEST_LEVEL 3
#define TEST_PHASE 1
#define TEST_VALUE_FIRST 7001
#define TEST_VALUE_LAST (-7002)

static int sTicking = NOT_TICKING; // the menu in CoopMenu_Tick
static bool sLevelTooHigh;         // the game names a level the session will not start
static int sLevelReads;
static int sEnters[MENUS];
static CoopLevelStart sEntered[MENUS];
static int sSessionsEnded;

static void Level_FromGame(CoopLevelStart* out) {
    memset(out, 0, sizeof(*out));
    out->level = sLevelTooHigh ? COOP_LEVEL_START_LEVELS : TEST_LEVEL;
    out->phase = TEST_PHASE;
    out->values[0] = TEST_VALUE_FIRST;
    out->values[COOP_LEVEL_START_VALUES - 1] = TEST_VALUE_LAST;
    sLevelReads++;
}

// A game can only enter a level from where it calls CoopMenu_Tick.
static void Level_Enter(const CoopLevelStart* start) {
    EXPECT(sTicking != NOT_TICKING, "a game entered a level outside CoopMenu_Tick");
    sEntered[sTicking] = *start;
    sEnters[sTicking]++;
}

static void Session_Ended(void) {
    sSessionsEnded++;
}

static void Level_Reset(void) {
    sLevelTooHigh = false;
    sLevelReads = 0;
    sSessionsEnded = 0;
    memset(sEnters, 0, sizeof(sEnters));
    memset(sEntered, 0, sizeof(sEntered));
}

// ---- Virtual network: the real transport core, a fake clock, a fixed delay ----

#define SIM_HOST 1
#define SIM_FIRST_CLIENT 2
#define SIM_NODES 6
#define SIM_MAX_PACKETS 8192
#define SIM_STEP_MS 5
#define SIM_PORT 40000
#define SIM_ADDRESS "sim"

typedef struct {
    uint32_t due;
    CoopNetAddr from;
    CoopNetAddr to;
    uint16_t size;
    uint8_t data[COOP_NET_MAX_DATAGRAM];
} SimPacket;

static struct {
    SimPacket* packets;
    uint32_t count;
    uint32_t now;
    uint32_t latencyMs;          // each way
    uint32_t extraMs[SIM_NODES]; // each way, on top, for packets to or from this node
    CoopNetCore* cores[SIM_NODES];
    int nextClient;
    bool failHost; // the port is taken
    int hostCalls;
    int connectCalls;
    uint16_t lastPort;
    uint32_t lastTimeoutMs; // what the menu gave the last transport it opened
    uint32_t picksSent;
    bool failPicks; // the transport's window is full
    bool dropPongs; // nobody gets a round trip
} sNet;

static void Sim_Tx(void* user, CoopNetAddr to, const uint8_t* data, uint16_t size) {
    SimPacket* p;

    // The message id is at byte 8 of a datagram, see PROTOCOL.md.
    if (sNet.dropPongs && size >= 10 && (data[8] | (data[9] << 8)) == COOP_MSG_PONG) {
        return;
    }
    if (sNet.count == SIM_MAX_PACKETS) {
        Fail("the virtual network ran out of room");
    }
    p = &sNet.packets[sNet.count++];
    p->from = (CoopNetAddr) (uintptr_t) user;
    p->due = sNet.now + sNet.latencyMs + (p->from < SIM_NODES ? sNet.extraMs[p->from] : 0) +
             (to < SIM_NODES ? sNet.extraMs[to] : 0);
    p->to = to;
    p->size = size;
    memcpy(p->data, data, size);
}

typedef struct {
    CoopTransport base;
    CoopNetCore* core;
    int addr;
} SimTransport;

static bool SimT_Send(CoopTransport* t, int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane) {
    if (msgId == COOP_MSG_SESSION_PICK) {
        if (sNet.failPicks) {
            return false;
        }
        sNet.picksSent++;
    }
    return CoopNetCore_Send(((SimTransport*) t)->core, peer, msgId, data, size, lane, sNet.now);
}

static bool SimT_Poll(CoopTransport* t, CoopNetEvent* ev) {
    return CoopNetCore_Poll(((SimTransport*) t)->core, ev);
}

static void SimT_Disconnect(CoopTransport* t, int peer) {
    CoopNetCore_Disconnect(((SimTransport*) t)->core, peer);
}

static bool SimT_Stats(CoopTransport* t, int peer, CoopNetStats* out) {
    return CoopNetCore_Stats(((SimTransport*) t)->core, peer, out);
}

static void SimT_Close(CoopTransport* t) {
    SimTransport* st = (SimTransport*) t;

    CoopNetCore_Destroy(st->core);
    sNet.cores[st->addr] = NULL;
    free(st);
}

static const CoopTransportOps sSimOps = { SimT_Send, SimT_Poll, SimT_Disconnect, SimT_Stats, SimT_Close };

// The menu's own config, as the real transports would get it.
static CoopTransport* Sim_Transport(int addr, const CoopNetConfig* menuConfig) {
    CoopNetConfig config = { 0 };
    SimTransport* st = calloc(1, sizeof(*st));

    if (menuConfig != NULL) {
        config = *menuConfig;
    }

    st->base.ops = &sSimOps;
    st->addr = addr;
    if (addr == SIM_HOST) {
        st->core = CoopNetCore_CreateHost(&config, Sim_Tx, (void*) (uintptr_t) addr);
    } else {
        st->core = CoopNetCore_CreateClient(&config, Sim_Tx, (void*) (uintptr_t) addr, SIM_HOST,
                                            (uint32_t) (addr * 7919u + sNet.now + 1u), sNet.now);
    }
    sNet.cores[addr] = st->core;
    return &st->base;
}

static CoopTransport* Sim_Host(uint16_t port, const CoopNetConfig* config) {
    sNet.hostCalls++;
    sNet.lastPort = port;
    sNet.lastTimeoutMs = config != NULL ? config->timeoutMs : 0;
    return sNet.failHost ? NULL : Sim_Transport(SIM_HOST, config);
}

// The node a client gets goes round, so a client that left can come back while others stay.
static CoopTransport* Sim_Connect(const char* host, uint16_t port, const CoopNetConfig* config) {
    int addr;

    sNet.connectCalls++;
    sNet.lastPort = port;
    sNet.lastTimeoutMs = config != NULL ? config->timeoutMs : 0;
    if (strcmp(host, SIM_ADDRESS) != 0) {
        return NULL;
    }
    for (addr = sNet.nextClient;; addr = addr + 1 == SIM_NODES ? SIM_FIRST_CLIENT : addr + 1) {
        if (sNet.cores[addr] == NULL) {
            sNet.nextClient = addr + 1 == SIM_NODES ? SIM_FIRST_CLIENT : addr + 1;
            return Sim_Transport(addr, config);
        }
    }
}

static uint16_t Sim_Port(CoopTransport* t) {
    (void) t;
    return sNet.lastPort != 0 ? sNet.lastPort : SIM_PORT;
}

static const CoopMenuBackend sSimBackend = { Sim_Host,       Sim_Connect, Sim_Port,     Id_Test,
                                             Level_FromGame, Level_Enter, Session_Ended };

static void Sim_Reset(uint32_t latencyMs) {
    int n;

    if (sNet.packets == NULL) {
        sNet.packets = malloc(sizeof(SimPacket) * SIM_MAX_PACKETS);
    }
    for (n = 0; n < SIM_NODES; n++) {
        EXPECT(sNet.cores[n] == NULL, "the last scenario left a transport open");
    }
    sNet.count = 0;
    sNet.now = 0xFFFFF000u; // the clock wraps a few seconds in
    sNet.latencyMs = latencyMs;
    sNet.nextClient = SIM_FIRST_CLIENT;
    sNet.failHost = false;
    sNet.hostCalls = 0;
    sNet.connectCalls = 0;
    sNet.lastPort = 0;
    sNet.picksSent = 0;
    sNet.failPicks = false;
    sNet.dropPongs = false;
    memset(sNet.extraMs, 0, sizeof(sNet.extraMs));
    sIdentityReads = 0;
    sBuild = "test-build";
    sPlatform = NULL;
    Level_Reset();
}

static void Sim_Step(void) {
    uint32_t i = 0;
    int n;

    sNet.now += SIM_STEP_MS;
    while (i < sNet.count) {
        SimPacket* p = &sNet.packets[i];

        if ((int32_t) (sNet.now - p->due) < 0) {
            i++;
            continue;
        }
        if (p->to < SIM_NODES && sNet.cores[p->to] != NULL) {
            CoopNetCore_Receive(sNet.cores[p->to], p->from, p->data, p->size, sNet.now);
        }
        *p = sNet.packets[--sNet.count];
    }
    for (n = 0; n < SIM_NODES; n++) {
        if (sNet.cores[n] != NULL) {
            CoopNetCore_Update(sNet.cores[n], sNet.now);
        }
    }
}

// ---- Menus on the virtual network ----

static CoopMenu sMenus[MENUS];
#define HOST (&sMenus[0])
#define A (&sMenus[1])
#define B (&sMenus[2])
#define C (&sMenus[3])
#define D (&sMenus[4])

static void Menus_Init(uint32_t latencyMs) {
    int i;

    Sim_Reset(latencyMs);
    for (i = 0; i < MENUS; i++) {
        CoopMenu_Init(&sMenus[i], &sSimBackend);
    }
}

static void Menus_Close(void) {
    int i;

    for (i = 0; i < MENUS; i++) {
        CoopMenu_Leave(&sMenus[i]);
    }
}

static void Run(uint32_t ms) {
    uint32_t t;
    int i;

    for (t = 0; t < ms; t += SIM_STEP_MS) {
        Sim_Step();
        for (i = 0; i < MENUS; i++) {
            sTicking = i;
            CoopMenu_Tick(&sMenus[i], sNet.now);
            sTicking = NOT_TICKING;
        }
    }
}

static const CoopMenuView* View(const CoopMenu* m) {
    static CoopMenuView views[4];
    static int next;
    CoopMenuView* v = &views[next];

    next = (next + 1) % 4;
    CoopMenu_View(m, v);
    return v;
}

static const CoopMenuChoice* Choice(const CoopMenuView* v, uint8_t character) {
    int i;

    for (i = 0; i < COOP_MENU_CHOICES; i++) {
        if (v->choices[i].character == character) {
            return &v->choices[i];
        }
    }
    Fail("a wingman is missing from the choices");
    return NULL;
}

static void Start_Host(void) {
    EXPECT(CoopMenu_Host(HOST, "45000"), "hosting did not start");
}

static void Join(CoopMenu* m) {
    EXPECT(CoopMenu_Join(m, SIM_ADDRESS, "45000", sNet.now), "joining did not start");
    Run(500);
    EXPECT(View(m)->phase == COOP_MENU_JOINED, "the client did not get in");
}

// A host and clients A, B and C in the session.
static void Start_Full(void) {
    Start_Host();
    Join(A);
    Join(B);
    Join(C);
}

static void Pick(CoopMenu* m, uint8_t character) {
    EXPECT(CoopMenu_Pick(m, character), "a free character could not be picked");
    Run(500);
}

// ---- Scenarios ----

static void Test_Idle(void) {
    const CoopMenuView* v;
    int i;

    sScenario = "idle";
    Menus_Init(0);
    Run(1000);
    v = View(A);
    EXPECT(v->phase == COOP_MENU_IDLE, "a new menu is not idle");
    Expect_Has(v->status, "Not in a session", "the status does not say so");
    EXPECT(v->message[0] == '\0', "a new menu has something to say");
    EXPECT(!v->showChoices, "a new menu offers characters");
    for (i = 0; i < COOP_SESSION_PLAYERS; i++) {
        EXPECT(!v->players[i].present, "a new menu lists a player");
    }
    EXPECT(!CoopMenu_Pick(A, FALCO), "a pick went through without a session");
    CoopMenu_Leave(A);
    EXPECT(sNet.hostCalls == 0 && sNet.connectCalls == 0 && sIdentityReads == 0,
           "without a session the menu opened a transport or read the identity");
    EXPECT(!v->showStart && !v->canStart && v->level[0] == '\0', "a new menu speaks of a level");
    EXPECT(!CoopMenu_Start(A), "a level started without a session");
    EXPECT(sLevelReads == 0 && sSessionsEnded == 0, "without a session the menu touched the game");
}

static void Bad_Join(const char* address, const char* port, const char* needle) {
    int calls = sNet.connectCalls;

    EXPECT(!CoopMenu_Join(A, address, port, sNet.now), "a bad address or port was taken");
    EXPECT(sNet.connectCalls == calls, "a bad address or port reached the transport");
    EXPECT(View(A)->phase == COOP_MENU_IDLE, "a bad address or port left the idle phase");
    Expect_Has(View(A)->message, needle, "the message does not say what is wrong");
}

static void Test_BadInput(void) {
    static const char* const badPorts[] = { "", "abc", "12x", "-1", "+1", " 1", "65536", "99999999999999999999" };
    size_t i;

    sScenario = "bad_input";
    Menus_Init(0);
    for (i = 0; i < sizeof(badPorts) / sizeof(badPorts[0]); i++) {
        EXPECT(!CoopMenu_Host(HOST, badPorts[i]), "a bad port was taken");
        Expect_Has(View(HOST)->message, "port", "the message does not name the port");
        Bad_Join(SIM_ADDRESS, badPorts[i], "port");
    }
    EXPECT(sNet.hostCalls == 0, "a bad port reached the transport");
    Bad_Join(SIM_ADDRESS, "0", "port"); // a host can take any free port, a client has to name one
    Bad_Join("", "45000", "address");
    Bad_Join(NULL, "45000", "address");

    // The wrong input leaves nothing behind.
    EXPECT(CoopMenu_Host(HOST, "65535"), "the top port was refused");
    EXPECT(sNet.lastPort == 65535, "the port did not reach the transport");
    // A level load stalls the game, and the transport's 5 s default is not the menu's choice (#132).
    EXPECT(sNet.lastTimeoutMs == COOP_MENU_TIMEOUT_MS, "the host's transport did not get the menu's time-out");
    EXPECT(View(HOST)->message[0] == '\0', "the old message stayed");
    sNet.lastTimeoutMs = 0;
    EXPECT(CoopMenu_Join(A, SIM_ADDRESS, "45000", sNet.now), "joining did not start");
    EXPECT(sNet.lastTimeoutMs == COOP_MENU_TIMEOUT_MS, "the client's transport did not get the menu's time-out");
    EXPECT(View(A)->message[0] == '\0', "the old message stayed");
    Menus_Close();
}

static void Test_TransportFails(void) {
    sScenario = "transport_fails";
    Menus_Init(0);
    sNet.failHost = true;
    EXPECT(!CoopMenu_Host(HOST, "45000"), "hosting reported success with no transport");
    EXPECT(View(HOST)->phase == COOP_MENU_IDLE, "no transport left the idle phase");
    Expect_Has(View(HOST)->message, "45000", "the message does not name the port");

    EXPECT(!CoopMenu_Join(A, "nowhere.invalid", "45000", sNet.now), "joining reported success with no transport");
    EXPECT(View(A)->phase == COOP_MENU_IDLE, "no transport left the idle phase");
    Expect_Has(View(A)->message, "nowhere.invalid", "the message does not name the address");

    sNet.failHost = false;
    Start_Host();
    Menus_Close();
}

static void Test_HostAlone(void) {
    const CoopMenuView* v;

    sScenario = "host_alone";
    Menus_Init(0);
    EXPECT(CoopMenu_Host(HOST, "0"), "hosting on a free port did not start");
    v = View(HOST);
    EXPECT(v->phase == COOP_MENU_HOSTING, "the phase is not hosting");
    Expect_Has(v->status, "40000", "the status does not name the port the transport chose");
    Expect_Has(v->status, "1 of 4", "the status does not count the players");
    EXPECT(v->players[0].present && v->players[0].character == FOX, "the host is not Fox");
    Expect_Has(v->players[0].text, "Player 1 (you)", "the host's line does not name it");
    Expect_Has(v->players[0].text, "Fox", "the host's line does not say Fox");
    Expect_Lacks(v->players[0].text, "ms", "the host shows a ping to itself");
    EXPECT(!v->players[0].hasPing, "the host has a ping to itself");
    EXPECT(!v->players[1].present, "an empty slot shows a player");
    EXPECT(!v->showChoices, "the host is offered characters");
    EXPECT(!CoopMenu_Pick(HOST, FALCO), "the host picked a wingman");
    EXPECT(sNet.picksSent == 0, "a host's pick went out");
    EXPECT(View(HOST)->message[0] == '\0', "the menu asked the session for a host's pick");

    EXPECT(!CoopMenu_Host(HOST, "0"), "a second session started");
    EXPECT(!CoopMenu_Join(HOST, SIM_ADDRESS, "45000", sNet.now), "a host joined something");
    EXPECT(sNet.hostCalls == 1 && sNet.connectCalls == 0, "a second transport was opened");
    Menus_Close();
    EXPECT(View(HOST)->phase == COOP_MENU_IDLE, "leaving did not end hosting");
}

static void Test_Join(void) {
    const CoopMenuView* v;
    int i;

    sScenario = "join";
    Menus_Init(0);
    Start_Host();
    EXPECT(CoopMenu_Join(A, SIM_ADDRESS, "45000", sNet.now), "joining did not start");
    v = View(A);
    EXPECT(v->phase == COOP_MENU_CONNECTING, "the phase is not connecting");
    Expect_Has(v->status, "sim:45000", "the status does not say where to");
    EXPECT(!v->showChoices, "characters on offer before the welcome");
    EXPECT(!CoopMenu_Pick(A, FALCO), "a pick went through before the welcome");
    EXPECT(sNet.picksSent == 0, "a pick went out before the welcome");

    Run(500);
    v = View(A);
    EXPECT(v->phase == COOP_MENU_JOINED, "the client did not get in");
    Expect_Has(v->status, "player 2", "the status does not say which player we are");
    EXPECT(v->message[0] == '\0', "a clean join has something to say");
    EXPECT(v->players[0].character == FOX, "the host is not Fox on the client");
    Expect_Has(v->players[0].text, "Player 1:", "the host's line is wrong on the client");
    EXPECT(v->players[1].present && v->players[1].character == UNPICKED, "the client holds something at the start");
    Expect_Has(v->players[1].text, "Player 2 (you)", "the client's line does not name it");
    Expect_Has(v->players[1].text, "no character", "the client's line does not say it holds nothing");
    EXPECT(!v->players[2].present, "an empty slot shows a player");
    EXPECT(v->showChoices, "no characters on offer");
    for (i = 0; i < COOP_MENU_CHOICES; i++) {
        EXPECT(v->choices[i].enabled, "a free character is not on offer");
    }
    Expect_Has(Choice(v, FALCO)->label, "Falco", "a choice has the wrong name");
    Expect_Has(Choice(v, SLIPPY)->label, "Slippy", "a choice has the wrong name");
    Expect_Has(Choice(v, PEPPY)->label, "Peppy", "a choice has the wrong name");
    EXPECT(!CoopMenu_Pick(A, FOX), "Fox was picked");
    EXPECT(!CoopMenu_Pick(A, UNPICKED), "holding nothing was picked");
    EXPECT(!CoopMenu_Pick(A, COOP_CHARACTER_COUNT), "a value that is no character was picked");
    EXPECT(sNet.picksSent == 0, "a pick out of range went out");
    EXPECT(View(A)->message[0] == '\0', "the menu asked the session for something that is no choice");

    v = View(HOST);
    Expect_Has(v->status, "2 of 4", "the host does not count the client");
    EXPECT(v->players[1].present && v->players[1].character == UNPICKED, "the host does not list the client");
    Expect_Has(v->players[1].text, "Player 2:", "the client's line is wrong on the host");
    Expect_Has(v->message, "Player 2 joined", "the host is not told");
    Menus_Close();
    EXPECT(View(HOST)->message[0] == '\0', "leaving on purpose left the old message");
}

static void Test_TakenCannotBeChosen(void) {
    const CoopMenuView* v;
    uint32_t sent;

    sScenario = "taken";
    Menus_Init(0);
    Start_Full();

    // While the answer is under way nothing else can be chosen.
    EXPECT(CoopMenu_Pick(A, FALCO), "a free character could not be picked");
    v = View(A);
    EXPECT(!Choice(v, FALCO)->enabled && !Choice(v, SLIPPY)->enabled && !Choice(v, PEPPY)->enabled,
           "something can be chosen while a pick is under way");
    Expect_Has(Choice(v, FALCO)->label, "asking", "the choice does not say a pick is under way");
    sent = sNet.picksSent;
    EXPECT(!CoopMenu_Pick(A, SLIPPY), "a second pick went through while one was under way");
    EXPECT(sNet.picksSent == sent, "a second pick went out");
    Run(500);

    v = View(A);
    EXPECT(v->players[1].character == FALCO, "the pick did not reach the roster");
    EXPECT(!Choice(v, FALCO)->enabled, "our own character can be chosen again");
    Expect_Has(Choice(v, FALCO)->label, "you", "our own character is not marked");
    EXPECT(Choice(v, SLIPPY)->enabled && Choice(v, PEPPY)->enabled, "the free characters are not on offer");

    // Falco is taken, for everyone else, and nothing goes out for it.
    v = View(B);
    EXPECT(v->players[1].character == FALCO, "player 3 does not see the pick");
    EXPECT(!Choice(v, FALCO)->enabled, "a taken character can be chosen");
    Expect_Has(Choice(v, FALCO)->label, "player 2", "the choice does not say who holds it");
    EXPECT(Choice(v, SLIPPY)->enabled && Choice(v, PEPPY)->enabled, "the free characters are not on offer");
    sent = sNet.picksSent;
    EXPECT(!CoopMenu_Pick(B, FALCO), "a taken character was chosen");
    EXPECT(!CoopMenu_Pick(A, FALCO), "picking what we hold was taken");
    EXPECT(sNet.picksSent == sent, "a pick for a taken character went out");
    Run(500);
    EXPECT(View(B)->players[2].character == UNPICKED, "a refused player holds something");
    EXPECT(View(HOST)->players[1].character == FALCO, "the holder lost the character");
    Expect_Lacks(View(HOST)->message, "taken", "the host saw a pick that should not have gone out");

    // The last one left.
    Pick(B, SLIPPY);
    v = View(C);
    EXPECT(!Choice(v, FALCO)->enabled && !Choice(v, SLIPPY)->enabled, "a taken character can be chosen");
    EXPECT(Choice(v, PEPPY)->enabled, "the last free character is not on offer");
    Pick(C, PEPPY);
    v = View(HOST);
    EXPECT(v->players[0].character == FOX && v->players[1].character == FALCO && v->players[2].character == SLIPPY &&
               v->players[3].character == PEPPY,
           "the host's roster is wrong");
    Expect_Has(v->status, "4 of 4", "the host does not count four");
    Menus_Close();
}

// Both see Slippy free and ask in the same frame, so the menu cannot stop it: the host has to.
static void Test_Race(void) {
    const CoopMenuView* vb;
    const CoopMenuView* vc;
    const CoopMenuView* loser;
    CoopMenu* loserMenu;

    sScenario = "race";
    Menus_Init(20);
    Start_Full();
    EXPECT(CoopMenu_Pick(B, SLIPPY) && CoopMenu_Pick(C, SLIPPY), "a free character could not be picked");
    Run(500);
    vb = View(B);
    vc = View(C);
    EXPECT((vb->players[2].character == SLIPPY) != (vc->players[3].character == SLIPPY), "not exactly one won");
    EXPECT(vb->players[2].character == vc->players[2].character && vb->players[3].character == vc->players[3].character,
           "the two disagree on who won");
    loserMenu = vb->players[2].character == SLIPPY ? C : B;
    loser = View(loserMenu);
    Expect_Has(loser->message, "Slippy is already taken", "the loser is not told");
    EXPECT(!Choice(loser, SLIPPY)->enabled, "the lost character can be chosen");
    EXPECT(Choice(loser, FALCO)->enabled && Choice(loser, PEPPY)->enabled, "the loser cannot pick again");
    Expect_Has(View(HOST)->message, "already taken", "the host is not told");
    Pick(loserMenu, PEPPY);
    EXPECT(View(loserMenu)->players[loserMenu == B ? 2 : 3].character == PEPPY, "the loser could not pick again");
    Menus_Close();
}

// Another player's pick reaches us while our own is under way: that is not our answer.
static void Test_OtherRoster(void) {
    const CoopMenuView* v;

    sScenario = "other_roster";
    Menus_Init(20);
    Start_Host();
    Join(A);
    Join(B);
    EXPECT(CoopMenu_Pick(B, SLIPPY), "a free character could not be picked");
    Run(30); // at the host, on its way back
    EXPECT(View(A)->players[2].character == UNPICKED, "the roster came back sooner than this test needs");
    EXPECT(CoopMenu_Pick(A, FALCO), "a free character could not be picked");
    Run(15);
    v = View(A);
    EXPECT(v->players[2].character == SLIPPY, "the other player's pick did not arrive");
    EXPECT(v->players[1].character == UNPICKED, "our own answer came sooner than this test needs");
    EXPECT(!Choice(v, PEPPY)->enabled, "another player's pick ended ours");
    Expect_Has(Choice(v, FALCO)->label, "asking", "another player's pick ended ours");
    Run(500);
    EXPECT(View(A)->players[1].character == FALCO, "the pick did not arrive");
    EXPECT(Choice(View(A), PEPPY)->enabled, "the pick stayed under way");
    Menus_Close();
}

static void Test_PickNotSent(void) {
    const CoopMenuView* v;

    sScenario = "pick_not_sent";
    Menus_Init(0);
    Start_Host();
    Join(A);
    sNet.failPicks = true;
    EXPECT(!CoopMenu_Pick(A, FALCO), "a pick that could not go out reported success");
    v = View(A);
    Expect_Has(v->message, "Try again", "the player is not told");
    EXPECT(Choice(v, FALCO)->enabled, "a pick that did not go out counts as under way");
    sNet.failPicks = false;
    Pick(A, FALCO);
    EXPECT(View(HOST)->players[1].character == FALCO, "the next try did not arrive");
    EXPECT(View(A)->message[0] == '\0', "the old message stayed");
    Menus_Close();
}

static void Test_ChangePick(void) {
    const CoopMenuView* v;

    sScenario = "change_pick";
    Menus_Init(0);
    Start_Host();
    Join(A);
    Join(B);
    Pick(A, FALCO);
    EXPECT(!Choice(View(B), FALCO)->enabled, "a taken character can be chosen");
    Pick(A, PEPPY);
    v = View(B);
    EXPECT(v->players[1].character == PEPPY, "the change did not arrive");
    EXPECT(Choice(v, FALCO)->enabled, "a character that was let go is still taken");
    EXPECT(!Choice(v, PEPPY)->enabled, "the new pick can be chosen");
    Pick(B, FALCO);
    EXPECT(View(HOST)->players[2].character == FALCO, "a character that was let go could not be picked");
    Menus_Close();
}

static void Test_Leave(void) {
    const CoopMenuView* v;

    sScenario = "leave";
    Menus_Init(0);
    Start_Host();
    Join(A);
    Join(B);
    Pick(A, FALCO);
    CoopMenu_Leave(A);
    EXPECT(View(A)->phase == COOP_MENU_IDLE, "leaving did not end the session");
    EXPECT(View(A)->message[0] == '\0', "leaving on purpose has something to say");
    Run(500);
    v = View(HOST);
    Expect_Has(v->status, "2 of 4", "the host still counts the one who left");
    EXPECT(!v->players[1].present, "the host still lists the one who left");
    Expect_Has(v->message, "Player 2 left", "the host is not told");
    v = View(B);
    EXPECT(!v->players[1].present, "player 3 still lists the one who left");
    EXPECT(Choice(v, FALCO)->enabled, "the character of a player who left is still taken");

    // The same menu can go back in, with nothing left over.
    Join(A);
    v = View(A);
    EXPECT(v->players[1].character == UNPICKED, "a player came back holding something");
    Pick(A, SLIPPY);
    EXPECT(View(HOST)->players[1].character == SLIPPY, "a player who came back could not pick");
    Menus_Close();
}

static void Test_HostCloses(void) {
    const CoopMenuView* v;

    sScenario = "host_closes";
    Menus_Init(0);
    Start_Host();
    Join(A);
    EXPECT(CoopMenu_Pick(A, FALCO), "a free character could not be picked"); // under way when the host goes
    CoopMenu_Leave(HOST);
    Run(500);
    v = View(A);
    EXPECT(v->phase == COOP_MENU_IDLE, "the client did not end with the host");
    Expect_Has(v->status, "Not in a session", "the status does not say so");
    Expect_Has(v->message, "closed the connection", "the client is not told why");
    EXPECT(!v->players[0].present && !v->showChoices, "the session stayed on the screen");

    // A new session starts clean: the old pick is not still under way, and the old message is gone.
    Start_Host();
    Join(A);
    v = View(A);
    EXPECT(v->message[0] == '\0', "the old message stayed");
    EXPECT(Choice(v, FALCO)->enabled, "the old pick is still under way");
    Menus_Close();
}

static void Test_Refused(void) {
    const CoopMenuView* v;

    sScenario = "refused";
    Menus_Init(0);
    Start_Host();
    sBuild = "other-build";
    EXPECT(CoopMenu_Join(A, SIM_ADDRESS, "45000", sNet.now), "joining did not start");
    Run(500);
    v = View(A);
    EXPECT(v->phase == COOP_MENU_IDLE, "a refused client stayed");
    Expect_Has(v->message, "other-build", "the client is not told what differs");
    Expect_Has(v->message, "test-build", "the client is not told what differs");
    v = View(HOST);
    EXPECT(v->phase == COOP_MENU_HOSTING, "a refusal ended hosting");
    Expect_Has(v->status, "1 of 4", "the host counts the refused player");
    Expect_Has(v->message, "other-build", "the host is not told what differs");
    Menus_Close();
}

static void Test_RefusedPlatform(void) {
    const CoopMenuView* v;

    sScenario = "refused_platform";
    Menus_Init(0);
    Start_Host();
    sPlatform = "Other platform";
    EXPECT(CoopMenu_Join(A, SIM_ADDRESS, "45000", sNet.now), "joining did not start");
    Run(500);
    v = View(A);
    EXPECT(v->phase == COOP_MENU_IDLE, "a refused client stayed");
    Expect_Has(v->message, "Different platforms: host ", "the client is not told what differs");
    Expect_Has(v->message, "joining player Other platform", "the client is not told what differs");
    v = View(HOST);
    EXPECT(v->phase == COOP_MENU_HOSTING, "a refusal ended hosting");
    Expect_Has(v->status, "1 of 4", "the host counts the refused player");
    Expect_Has(v->message, "A player was refused.", "the host is not told");
    Expect_Has(v->message, "joining player Other platform", "the host is not told what differs");
    Menus_Close();
}

static void Test_AnyPlatform(void) {
    const CoopMenuView* v;

    sScenario = "any_platform";
    Menus_Init(0);
    Start_Host();
    sPlatform = "Other platform";
    CoopSession_AnyPlatform(true);
    EXPECT(CoopMenu_Join(A, SIM_ADDRESS, "45000", sNet.now), "joining did not start");
    Run(500);
    CoopSession_AnyPlatform(false);
    v = View(A);
    EXPECT(v->phase == COOP_MENU_JOINED, "the test switch did not let the client in");
    Expect_Has(v->message, "joining player Other platform", "the client is not told what differs");
    v = View(HOST);
    Expect_Has(v->status, "2 of 4", "the host does not count the player");
    Expect_Has(v->message, "Player 2 joined.", "the host is not told");
    Expect_Has(v->message, "joining player Other platform", "the host is not told what differs");
    Menus_Close();
}

static void Test_NoHost(void) {
    const CoopMenuView* v;

    sScenario = "no_host";
    Menus_Init(0);
    EXPECT(CoopMenu_Join(A, SIM_ADDRESS, "45000", sNet.now), "joining did not start");
    Run(COOP_MENU_TIMEOUT_MS - 1000);
    EXPECT(View(A)->phase == COOP_MENU_CONNECTING, "gave up too early");
    Run(2000);
    v = View(A);
    EXPECT(v->phase == COOP_MENU_IDLE, "still connecting to nobody");
    Expect_Has(v->message, "timed out", "the client is not told why");
    Menus_Close();
}

static void Test_HostFull(void) {
    sScenario = "host_full";
    Menus_Init(0);
    Start_Full();
    EXPECT(CoopMenu_Join(D, SIM_ADDRESS, "45000", sNet.now), "joining did not start");
    Run(500);
    EXPECT(View(D)->phase == COOP_MENU_IDLE, "a fifth player stayed");
    Expect_Has(View(D)->message, "full", "the client is not told why");
    Expect_Has(View(HOST)->status, "4 of 4", "the fifth player changed the count");
    Menus_Close();
}

// A long frame: everything that piled up goes in one Tick.
static void Test_OneTick(void) {
    uint32_t t;

    sScenario = "one_tick";
    Menus_Init(0);
    Start_Host();
    EXPECT(CoopMenu_Join(A, SIM_ADDRESS, "45000", sNet.now), "joining did not start");
    EXPECT(CoopMenu_Join(B, SIM_ADDRESS, "45000", sNet.now), "joining did not start");
    EXPECT(CoopMenu_Join(C, SIM_ADDRESS, "45000", sNet.now), "joining did not start");
    for (t = 0; t < 300; t += SIM_STEP_MS) {
        Sim_Step();
        CoopMenu_Tick(A, sNet.now);
        CoopMenu_Tick(B, sNet.now);
        CoopMenu_Tick(C, sNet.now);
    }
    Expect_Has(View(HOST)->status, "1 of 4", "the test needs the host to have read nothing yet");
    CoopMenu_Tick(HOST, sNet.now);
    Expect_Has(View(HOST)->status, "4 of 4", "one Tick left events waiting");
    Menus_Close();
}

static void Expect_Ping(const CoopMenuPlayer* p, uint32_t truth, uint32_t lowest, uint32_t highest) {
    char want[32];

    EXPECT(p->hasPing, "no ping after a second of pings");
    if (p->pingMs != truth) {
        printf("FAIL %s: ping shows %u, the transport says %u\n", sScenario, (unsigned) p->pingMs, (unsigned) truth);
        exit(1);
    }
    if (p->pingMs < lowest || p->pingMs > highest) {
        printf("FAIL %s: ping %u, want %u to %u\n", sScenario, (unsigned) p->pingMs, (unsigned) lowest,
               (unsigned) highest);
        exit(1);
    }
    snprintf(want, sizeof(want), "%u ms", (unsigned) p->pingMs);
    Expect_Has(p->text, want, "the line does not show the ping");
}

static void Test_Ping(void) {
    const CoopMenuView* v;
    CoopNetStats stats;

    sScenario = "ping";
    Menus_Init(40);
    sNet.extraMs[SIM_FIRST_CLIENT + 1] = 30; // client B's link is slower, so the two pings differ
    Start_Host();
    EXPECT(CoopMenu_Join(A, SIM_ADDRESS, "45000", sNet.now), "joining did not start");

    // Before the first pong there is no number to show, and it must not read as 0 ms. The transport pings as soon as it
    // connects, so on a good link that is over before the welcome: this needs the pongs held back.
    sNet.dropPongs = true;
    Run(1000);
    v = View(A);
    EXPECT(v->phase == COOP_MENU_JOINED, "the client did not get in");
    EXPECT(!v->players[0].hasPing, "a ping before the first pong");
    Expect_Has(v->players[0].text, "not measured", "the client's line does not say the ping is unknown");
    Expect_Lacks(v->players[0].text, " ms", "the client's line shows a number it does not have");
    v = View(HOST);
    EXPECT(!v->players[1].hasPing, "a ping before the first pong");
    Expect_Has(v->players[1].text, "not measured", "the host's line does not say the ping is unknown");
    sNet.dropPongs = false;
    Run(1000);
    Join(B);
    Run(1000);

    v = View(A);
    EXPECT(CoopNetCore_Stats(sNet.cores[SIM_FIRST_CLIENT], 0, &stats), "client has no link to the host");
    Expect_Ping(&v->players[0], stats.rttMs, 80, 95);
    EXPECT(!v->players[1].hasPing, "a client shows a ping to itself");
    Expect_Lacks(v->players[1].text, " ms", "a client shows a ping to itself");
    EXPECT(!v->players[2].hasPing, "a client shows a ping to another client");
    Expect_Lacks(v->players[2].text, " ms", "a client shows a ping to another client");

    v = View(HOST);
    EXPECT(!v->players[0].hasPing, "the host shows a ping to itself");
    EXPECT(CoopNetCore_Stats(sNet.cores[SIM_HOST], 0, &stats), "host has no link to client 1");
    Expect_Ping(&v->players[1], stats.rttMs, 80, 95);
    EXPECT(CoopNetCore_Stats(sNet.cores[SIM_HOST], 1, &stats), "host has no link to client 2");
    Expect_Ping(&v->players[2], stats.rttMs, 140, 155);

    // A worse link shows.
    sNet.latencyMs = 100;
    Run(4000);
    v = View(A);
    if (v->players[0].pingMs < 180 || v->players[0].pingMs > 215) {
        printf("FAIL %s: ping %u on a link with a round trip of 200 ms\n", sScenario, (unsigned) v->players[0].pingMs);
        exit(1);
    }
    Menus_Close();
}

// A game message reaches the menu's poll. Nothing reads them yet, so it must pass by and change nothing.
static void Test_GameMessage(void) {
    static const uint8_t data[4] = { 1, 2, 3, 4 };
    const CoopMenuView* v;

    sScenario = "game_message";
    Menus_Init(0);
    Start_Host();
    Join(A);
    EXPECT(CoopSession_Send(HOST->session, 0, COOP_MSG_SESSION_END, data, sizeof(data), COOP_NET_RELIABLE),
           "the test message did not go out");
    EXPECT(CoopSession_Send(A->session, 0, COOP_MSG_SESSION_END, data, sizeof(data), COOP_NET_RELIABLE),
           "the test message did not go out");
    Run(500);
    v = View(A);
    EXPECT(v->phase == COOP_MENU_JOINED && v->message[0] == '\0', "a game message changed the client's menu");
    v = View(HOST);
    EXPECT(v->phase == COOP_MENU_HOSTING, "a game message changed the host's menu");
    Menus_Close();
}

static void Expect_NoEntry(void) {
    int i;

    EXPECT(sLevelReads == 0, "the game was asked for a level no one can start");
    for (i = 0; i < MENUS; i++) {
        EXPECT(sEnters[i] == 0, "a game entered a level no one started");
    }
}

// Start is the host's, and it stays grey while the session would turn it down.
static void Test_StartButton(void) {
    const CoopMenuView* v;

    sScenario = "start_button";
    Menus_Init(20);
    Start_Host();
    v = View(HOST);
    EXPECT(v->showStart && v->canStart, "a host alone cannot start");
    EXPECT(v->level[0] == '\0', "a host that can start is told to wait");
    Expect_Lacks(v->startLabel, "choose", "the button says to wait while it can be pressed");
    EXPECT(v->startLabel[0] != '\0', "the button has no label");

    Join(A);
    v = View(HOST);
    EXPECT(v->showStart && !v->canStart, "Start is lit with a client that holds nothing");
    Expect_Has(v->startLabel, "choose", "the grey button does not say what it waits for");
    EXPECT(!CoopMenu_Start(HOST), "a click went through with a client that holds nothing");
    v = View(A);
    EXPECT(!v->showStart && !v->canStart, "a client has a Start button");
    Expect_Has(v->level, "Waiting for the host", "a client is not told who starts");
    EXPECT(!CoopMenu_Start(A), "a client started the level");

    Pick(A, FALCO);
    EXPECT(View(HOST)->canStart, "Start stays grey with everyone holding a character");
    Join(B);
    EXPECT(!View(HOST)->canStart, "Start stays lit with a new client that holds nothing");
    EXPECT(!CoopMenu_Start(HOST), "a click went through with the last client holding nothing");
    CoopMenu_Leave(B);
    Run(500);
    EXPECT(View(HOST)->canStart, "a client who left holding nothing holds the start up");

    // A client on its way in may not be seen in the roster yet, and it holds nothing either.
    EXPECT(CoopMenu_Join(B, SIM_ADDRESS, "45000", sNet.now), "joining did not start");
    while (View(HOST)->canStart) {
        Run(SIM_STEP_MS);
        EXPECT(View(B)->phase == COOP_MENU_CONNECTING, "the host never saw the client coming");
    }
    EXPECT(!View(HOST)->players[2].present, "this client is in the roster, so this proves nothing");
    EXPECT(!CoopMenu_Start(HOST), "a click went through with a client in the handshake");
    Run(500);
    Pick(B, SLIPPY);
    EXPECT(View(HOST)->canStart, "Start stays grey with everyone holding a character");
    Expect_NoEntry();

    // The session has the last word, and a start it turns down leaves the lobby open.
    sLevelTooHigh = true;
    EXPECT(!CoopMenu_Start(HOST), "a level that cannot be started went through");
    Expect_Has(View(HOST)->message, "Could not start", "the host is not told");
    EXPECT(View(HOST)->canStart && View(HOST)->showStart, "a start that was turned down closed the lobby");
    Menus_Close();
}

static void Expect_Entered(int menu, const uint8_t* roster) {
    const CoopLevelStart* start = &sEntered[menu];

    EXPECT(sEnters[menu] == 1, "a game did not enter the level, or did twice");
    EXPECT(start->level == TEST_LEVEL && start->phase == TEST_PHASE, "a game entered another level");
    EXPECT(start->values[0] == TEST_VALUE_FIRST, "a game entered with other values");
    EXPECT(start->values[COOP_LEVEL_START_VALUES - 1] == TEST_VALUE_LAST, "a game entered with other values");
    EXPECT(memcmp(start->roster, roster, COOP_SESSION_PLAYERS) == 0, "a game entered with another roster");
}

// The host and two clients, each with a character.
static void Start_Three(void) {
    Start_Host();
    Join(A);
    Join(B);
    Pick(A, FALCO);
    Pick(B, SLIPPY);
}

// One click: every game enters the level once, from the session and not from the click, and that closes the lobby.
static void Test_LevelStart(void) {
    static const uint8_t roster[COOP_SESSION_PLAYERS] = { FOX, FALCO, SLIPPY, NO_PLAYER };
    const CoopMenuView* v;
    uint32_t picks;
    int i;

    sScenario = "level_start";
    Menus_Init(20);
    Start_Three();
    picks = sNet.picksSent;
    Expect_Has(View(HOST)->message, "joined", "this test needs an old message to see it go");
    EXPECT(CoopMenu_Start(HOST), "Start did nothing");
    EXPECT(View(HOST)->message[0] == '\0', "the old message stayed");
    EXPECT(sLevelReads == 1, "the game was not asked what to start, or twice");
    EXPECT(!CoopMenu_Start(HOST), "a second click went through");
    EXPECT(sLevelReads == 1, "a second click asked the game");
    EXPECT(sEnters[0] == 0, "the host entered from the click, ahead of the session");
    Run(500);
    for (i = 0; i < 3; i++) {
        Expect_Entered(i, roster);
    }
    EXPECT(sEnters[3] == 0 && sEnters[4] == 0, "a menu with no session entered the level");
    EXPECT(sSessionsEnded == 0, "the level start ended a session");

    v = View(HOST);
    EXPECT(v->phase == COOP_MENU_HOSTING && !v->showStart && !v->canStart, "the host can still start");
    Expect_Has(v->level, "The level has started", "the host is not told");
    Expect_Has(v->players[1].text, "Falco", "the level start changed the host's list");
    for (i = 1; i < 3; i++) {
        v = View(&sMenus[i]);
        EXPECT(v->phase == COOP_MENU_JOINED && !v->showStart, "the level start changed a client's session");
        EXPECT(!v->showChoices, "a client is offered characters in the level");
        Expect_Has(v->level, "The level has started", "a client is not told");
        EXPECT(!CoopMenu_Pick(&sMenus[i], PEPPY), "a pick went through in the level");
        EXPECT(View(&sMenus[i])->message[0] == '\0', "a pick in the level left an error to read");
    }
    EXPECT(sNet.picksSent == picks, "a pick reached the host in the level");
    Run(1000);
    EXPECT(sEnters[0] == 1 && sEnters[1] == 1 && sEnters[2] == 1, "a game entered the level again");
    Menus_Close();
    EXPECT(sSessionsEnded == 3, "not every session end reached the game");
}

// A pick still on its way when the host starts is dropped; the level start says what the client holds.
static void Test_LevelPickInFlight(void) {
    static const uint8_t roster[COOP_SESSION_PLAYERS] = { FOX, FALCO, SLIPPY, NO_PLAYER };
    const CoopMenuView* v;

    sScenario = "level_pick_in_flight";
    Menus_Init(50);
    Start_Three();
    EXPECT(CoopMenu_Pick(B, PEPPY), "a free character could not be picked");
    EXPECT(CoopMenu_Start(HOST), "Start did nothing");
    Run(1000);
    Expect_Entered(2, roster);
    Expect_Entered(1, roster);
    Expect_Entered(0, roster);
    v = View(B);
    EXPECT(B->pendingPick == 0, "a dropped pick is still waited for");
    EXPECT(v->message[0] == '\0', "a dropped pick reads as an error");
    Expect_Has(v->players[2].text, "Slippy", "the client shows what it asked for, not what it holds");
    Menus_Close();
}

// Whoever ends a session, the game hears it once, and the next session starts from nothing.
static void Test_LevelEnds(void) {
    const CoopMenuView* v;

    sScenario = "level_ends";
    Menus_Init(20);
    Start_Host();
    Join(A);
    CoopMenu_Leave(A);
    EXPECT(sSessionsEnded == 1, "a client that left before a level did not end its session");
    Run(500);
    Join(A);
    Pick(A, FALCO);
    EXPECT(CoopMenu_Start(HOST), "Start did nothing");
    Run(500);
    EXPECT(sSessionsEnded == 1, "the level start ended a session");

    CoopMenu_Leave(HOST);
    EXPECT(sSessionsEnded == 2, "the host's own end did not reach its game");
    Run(500);
    EXPECT(sSessionsEnded == 3, "the host's end did not reach the client's game");
    v = View(A);
    EXPECT(v->phase == COOP_MENU_IDLE && v->level[0] == '\0', "a level outlives its session in the menu");
    Expect_Has(v->message, "closed the connection", "the client is not told why");
    v = View(HOST);
    EXPECT(!v->showStart && v->level[0] == '\0', "a level outlives its session in the menu");

    // A new session has a Start button again and enters its own level.
    Start_Host();
    v = View(HOST);
    EXPECT(v->showStart && v->canStart && v->level[0] == '\0', "the last level shows in a new session");
    EXPECT(CoopMenu_Start(HOST), "a new session could not start");
    Run(500);
    EXPECT(sEnters[0] == 2, "a new session did not enter its level");
    Menus_Close();
}

static int Run_Sim(void) {
    Test_Idle();
    Test_BadInput();
    Test_TransportFails();
    Test_HostAlone();
    Test_Join();
    Test_TakenCannotBeChosen();
    Test_Race();
    Test_OtherRoster();
    Test_PickNotSent();
    Test_ChangePick();
    Test_Leave();
    Test_HostCloses();
    Test_Refused();
    Test_RefusedPlatform();
    Test_AnyPlatform();
    Test_NoHost();
    Test_HostFull();
    Test_OneTick();
    Test_Ping();
    Test_GameMessage();
    Test_StartButton();
    Test_LevelStart();
    Test_LevelPickInFlight();
    Test_LevelEnds();
    printf("PASS: 24 scenarios\n");
    return 0;
}

// ---- Real UDP ----

static const CoopMenuBackend sUdpBackend = { CoopNetSdl_Host, CoopNetSdl_Connect, CoopNetSdl_Port, Id_Test,
                                             Level_FromGame,  Level_Enter,        Session_Ended };

typedef bool (*Until)(void);

static void Udp_Run(Until until, const char* what) {
    uint32_t start = SDL_GetTicks();

    while (!until()) {
        int i;

        if (SDL_GetTicks() - start > 10000) {
            Fail(what);
        }
        for (i = 0; i < 3; i++) {
            sTicking = i;
            CoopMenu_Tick(&sMenus[i], SDL_GetTicks());
            sTicking = NOT_TICKING;
        }
        SDL_Delay(2);
    }
}

static bool Udp_BothIn(void) {
    return View(A)->phase == COOP_MENU_JOINED && View(B)->phase == COOP_MENU_JOINED &&
           View(B)->players[1].character == UNPICKED && View(A)->players[2].character == UNPICKED;
}

static bool Udp_FalcoSeen(void) {
    return View(B)->players[1].character == FALCO;
}

static bool Udp_PingSeen(void) {
    return View(A)->players[0].hasPing && View(HOST)->players[1].hasPing && View(HOST)->players[2].hasPing;
}

static bool Udp_Gone(void) {
    return !View(HOST)->players[1].present && !View(B)->players[1].present;
}

static bool Udp_CanStart(void) {
    return View(HOST)->canStart;
}

static bool Udp_BothEntered(void) {
    return sEnters[0] == 1 && sEnters[2] == 1;
}

static bool Udp_Ended(void) {
    return View(B)->phase == COOP_MENU_IDLE;
}

static int Run_Udp(void) {
    char port[16];
    const CoopMenuView* v;
    int i;

    sScenario = "udp";
    for (i = 0; i < 3; i++) {
        CoopMenu_Init(&sMenus[i], &sUdpBackend);
    }
    EXPECT(CoopMenu_Host(HOST, "0"), "hosting on a free port did not start");
    EXPECT(HOST->port != 0, "the menu does not know the port it got");
    snprintf(port, sizeof(port), "%u", (unsigned) HOST->port);
    Expect_Has(View(HOST)->status, port, "the status does not show the port the transport chose");
    EXPECT(CoopMenu_Join(A, "127.0.0.1", port, SDL_GetTicks()), "joining did not start");
    EXPECT(CoopMenu_Join(B, "127.0.0.1", port, SDL_GetTicks()), "joining did not start");
    Udp_Run(Udp_BothIn, "two clients did not get in");
    EXPECT(CoopMenu_Pick(A, FALCO), "a free character could not be picked");
    Udp_Run(Udp_FalcoSeen, "the pick did not reach the other client");
    v = View(B);
    EXPECT(!Choice(v, FALCO)->enabled, "a taken character can be chosen");
    EXPECT(!CoopMenu_Pick(B, FALCO), "a taken character was chosen");
    Udp_Run(Udp_PingSeen, "no ping after 10 s");
    CoopMenu_Leave(A);
    Udp_Run(Udp_Gone, "a client that left is still listed");
    EXPECT(Choice(View(B), FALCO)->enabled, "the character of a player who left is still taken");
    EXPECT(!View(HOST)->canStart, "Start is lit with a client that holds nothing");
    EXPECT(CoopMenu_Pick(B, SLIPPY), "a free character could not be picked");
    Udp_Run(Udp_CanStart, "Start stayed grey with everyone holding a character");
    EXPECT(CoopMenu_Start(HOST), "Start did nothing");
    Udp_Run(Udp_BothEntered, "the host and the client did not both enter the level");
    EXPECT(memcmp(&sEntered[0], &sEntered[2], sizeof(sEntered[0])) == 0, "the two games entered different levels");
    EXPECT(sEntered[2].values[COOP_LEVEL_START_VALUES - 1] == TEST_VALUE_LAST, "the values did not cross");
    CoopMenu_Leave(HOST);
    Udp_Run(Udp_Ended, "the client did not end with the host");
    Expect_Has(View(B)->message, "closed the connection", "the client is not told why");
    printf("PASS: host, two clients, pick, ping, leave, level start over UDP\n");
    return 0;
}

int main(int argc, char** argv) {
    int code = 2;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 2 && strcmp(argv[1], "sim") == 0) {
        code = Run_Sim();
    } else if (argc == 2 && strcmp(argv[1], "udp") == 0) {
        code = Run_Udp();
    } else {
        printf("usage: coop_menu_test sim|udp\n");
    }
    free(sNet.packets);
    return code;
}
