// Tests src/port/coop/sync/handover.c alone (#189): no game, no session, no socket. handover_test.py compiles this file
// with handover.c and runs it. Two checks: the names a received file may have, so that no write can leave the game's
// folder or take another player's name, and the size caps.
//
// Prints `ok <name>: ...` or `FAIL <name>: ...` per test, `hostile names: <n>` and `RESULT: <passed>/<run>`.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "handover.h"

static int sPassed = 0;
static int sRun = 0;
static int sFailedHere = 0;

static void Fail(const char* test, const char* format, ...) {
    va_list args;

    printf("FAIL %s: ", test);
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    sFailedHere++;
}

static void Begin_Test(void) {
    sFailedHere = 0;
    sRun++;
}

static void End_Test(const char* test, const char* detail) {
    if (sFailedHere == 0) {
        sPassed++;
        printf("ok %s: %s\n", test, detail);
    }
}

#define CHECK(test, cond, ...)       \
    do {                             \
        if (!(cond)) {               \
            Fail(test, __VA_ARGS__); \
        }                            \
    } while (0)

// The receiver is player 1 (index 0), the sender player 2 (index 1).
static void Test_Names(void) {
    static const char* valid[] = { "coop-desync-0-player2.txt", "coop-desync-1906-player2.txt",
                                   "coop-desync-2000-player2-2.txt" };
    static const struct {
        const char* name;
        const char* why;
    } hostile[] = {
        { "../coop-desync-12-player2.txt", "a way out of the folder" },
        { "sub/coop-desync-12-player2.txt", "a path separator" },
        { "/etc/coop-desync-12-player2.txt", "an absolute path" },
        { "coop-desync-12-player1.txt", "the receiver's own player number" },
        { "coop-log-12-player2.txt", "another prefix" },
    };
    const char* test = "names";
    size_t i;

    Begin_Test();
    for (i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
        CHECK(test, CoopHandover_NameValid(valid[i], strlen(valid[i]), 1, 0), "refused %s", valid[i]);
    }
    for (i = 0; i < sizeof(hostile) / sizeof(hostile[0]); i++) {
        CHECK(test, !CoopHandover_NameValid(hostile[i].name, strlen(hostile[i].name), 1, 0), "took %s (%s)",
              hostile[i].name, hostile[i].why);
    }
    printf("hostile names: %d\n", (int) (sizeof(hostile) / sizeof(hostile[0])));
    End_Test(test, "3 report names of the sender pass; `..`, a separator, an absolute path, the receiver's own number "
                   "and another prefix fail");
}

static bool Send_Nothing(void* user, int peer, const uint8_t* data, uint16_t size) {
    (void) user;
    (void) peer;
    (void) data;
    (void) size;
    return true;
}

static uint16_t Start_Msg(uint8_t* out, uint8_t file, uint32_t size, const char* name) {
    size_t len = strlen(name);

    out[0] = COOP_HANDOVER_FILE_START;
    out[1] = file;
    out[2] = (uint8_t) size;
    out[3] = (uint8_t) (size >> 8);
    out[4] = (uint8_t) (size >> 16);
    out[5] = (uint8_t) (size >> 24);
    out[6] = (uint8_t) len;
    memcpy(&out[7], name, len);
    return (uint16_t) (7 + len);
}

// A host, player 1, with clients at peers 0 and 1, players 2 and 3; files are added before Begin.
static void Host_Init(CoopHandover* h, bool begin) {
    CoopHandoverConfig config = { Send_Nothing, NULL, NULL, 0 };

    CoopHandover_Init(h, &config);
    CoopHandover_AddPeer(h, 0, 1);
    CoopHandover_AddPeer(h, 1, 2);
    if (begin) {
        CoopHandover_Begin(h, COOP_HANDOVER_REASON_FAIL, 0, COOP_HANDOVER_MS);
    }
}

static void Test_Caps(void) {
    const char* test = "caps";
    uint8_t m[COOP_HANDOVER_MESSAGE_MAX];
    uint8_t* big = calloc(1, COOP_HANDOVER_FILE_MAX + 1);
    CoopHandover h;
    uint16_t size;
    int i;

    Begin_Test();
    // Received: size 0, above 2 MiB, a fifth file, and more than 4 MiB from all peers together.
    Host_Init(&h, true);
    size = Start_Msg(m, 0, 0, "coop-desync-9-player2.txt");
    CHECK(test, !CoopHandover_Receive(&h, 0, m, size), "took a file of size 0");
    size = Start_Msg(m, 1, COOP_HANDOVER_FILE_MAX + 1, "coop-desync-9-player2.txt");
    CHECK(test, !CoopHandover_Receive(&h, 0, m, size), "took a file above 2 MiB");
    size = Start_Msg(m, 4, 100, "coop-desync-9-player2.txt");
    CHECK(test, !CoopHandover_Receive(&h, 0, m, size), "took a fifth file");
    size = Start_Msg(m, 2, COOP_HANDOVER_FILE_MAX, "coop-desync-9-player2.txt");
    CHECK(test, CoopHandover_Receive(&h, 0, m, size), "refused 2 MiB from player 2");
    size = Start_Msg(m, 0, COOP_HANDOVER_FILE_MAX, "coop-desync-9-player3.txt");
    CHECK(test, CoopHandover_Receive(&h, 1, m, size), "refused 2 MiB from player 3");
    size = Start_Msg(m, 1, 1, "coop-desync-9-player3-2.txt");
    CHECK(test, !CoopHandover_Receive(&h, 1, m, size), "took a byte above 4 MiB in all");
    CHECK(test, h.totalIn == COOP_HANDOVER_TOTAL_MAX, "took %u bytes in all", (unsigned) h.totalIn);
    CoopHandover_Free(&h);

    // Sent: the same caps before anything goes.
    Host_Init(&h, false);
    CHECK(test, !CoopHandover_AddFile(&h, "coop-desync-1-player1.txt", big, 0), "sends an empty file");
    CHECK(test, !CoopHandover_AddFile(&h, "coop-desync-1-player1.txt", big, COOP_HANDOVER_FILE_MAX + 1),
          "sends a file above 2 MiB");
    for (i = 1; i <= COOP_HANDOVER_FILES; i++) {
        char name[48];

        snprintf(name, sizeof(name), (i == 1) ? "coop-desync-1-player1.txt" : "coop-desync-1-player1-%d.txt", i);
        CHECK(test, CoopHandover_AddFile(&h, name, big, 10), "refused file %d of %d", i, COOP_HANDOVER_FILES);
    }
    CHECK(test, !CoopHandover_AddFile(&h, "coop-desync-1-player1-9.txt", big, 10), "sends a fifth file");
    CoopHandover_Free(&h);
    free(big);
    End_Test(test, "received and sent: size 0, above 2 MiB and a fifth file are refused, and all peers together hand "
                   "in 4 MiB, not a byte more");
}

int main(void) {
    Test_Names();
    Test_Caps();
    printf("RESULT: %d/%d\n", sPassed, sRun);
    return (sPassed == sRun) ? 0 : 1;
}
