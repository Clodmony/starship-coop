#!/usr/bin/env python3
"""Mutation check for the co-op session: break one behaviour of session.c, rebuild, require its tests to fail.

    python tools/coop/session_mutants.py                 # every break; about three minutes
    python tools/coop/session_mutants.py --only host_hold_back_off,linger_clock_wrap
    python tools/coop/session_mutants.py --list
    python tools/coop/session_mutants.py --check         # only check that every pattern still matches; no build
    python tools/coop/session_mutants.py --asan          # the breaks only the sanitizers can see; not on Windows

Each break below replaces one piece of text in src/port/coop/session/session.c with a broken variant and names the
session_test.py scenarios that must fail with it. How a run goes, and how it guards the source it changes:
tools/coop/mutants.py.

The set was made fresh for #55, path by path through session.c. Add a break whenever you fix a bug in session.c: the
old, broken text as the replacement, and the scenario that found it.

Left out, because no test can tell them from the original (a test for one would have to read the session's insides):

- CoopSession_Join: dropping `s->client = CLIENT_CONNECTING` (it is 0, and calloc gave 0) or `s->clientSince = nowMs`
  (read only in WAIT_ANSWER, and the switch to that state sets it again).
- Host_Hello: dropping `size == 0 ||` before the welcome. The hello matched this identity, so its counts are in range,
  and the largest identity fits a message (Sim_Identity checks that), so Write cannot fail there.
- Host_Tick: dropping `memset(&p->identity, ...)` before a time-out's refusal. The identity of a peer is only read in
  JOINED, and that state comes after CoopIdentity_Read has written all of it.
- Client_Answer: dropping `s->client = CLIENT_ENDED` before a refusal's ENDED. Poll returns the pending end first,
  and Client_End sets the same state.
- Host_Refuse: writing 0 instead of `reasons >> 16` and `reasons >> 24`. No reason bit above 8 exists yet. Dropping
  those lines is in the table.
- Session_New: the calloc failure. The test program has no way to make calloc fail.
- Host_Rejoin: `!s->away[slot].on` alone in the last refusal. A slot that holds a character belongs to a joined peer or
  to a player who is away, and the lines above turn the first into the second, so only `slot == 0` can be true there.
  The break `rejoin_nobody_refused` drops the whole test.
- CoopSession_SetRestart: dropping `s->isHost` or `s->started`. A client never starts a level again, and
  CoopSession_StartLevel writes the restart point over whatever was set before it.
- The event queue's bound (Host_Queue): no path of the tests owes more than QUEUE_SIZE events in one poll.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "coop"))
import mutants  # noqa: E402
import session_test  # noqa: E402  (scenario names and how the test program is found)

# Texts that several breaks replace, in different ways.
SEND_GUARD = "if (msgId < COOP_MSG_SESSION_END || CoopSession_PeerIdentity(s, peer) == NULL) {"
CLIENT_IDENTITY = "return (peer == 0 && s->client == CLIENT_ACTIVE) ? &s->peers[0].identity : NULL;"
READ_ALL = "return read != 0 && read == size;"
REFUSE_GONE = "s->t->ops->disconnect(s->t, peer);\n        p->state = PEER_FREE;\n    } else {"
LINGER = "if (delivered || nowMs - p->since >= COOP_SESSION_REFUSE_LINGER_MS) {"
DROP_REFUSED = "s->t->ops->disconnect(s->t, i);\n                p->state = PEER_FREE;"
DELIVERED = "bool delivered = s->t->ops->stats(s->t, i, &stats) && stats.pending == 0;"
HOST_TIMEOUT = "} else if (p->state == PEER_WAIT_HELLO && nowMs - p->since >= COOP_SESSION_HANDSHAKE_MS) {"
CONNECTED_STATE = "p->state = PEER_WAIT_HELLO;\n                p->since = nowMs;"
PEER_JOINED_FLAG = "bool joined = p->state == PEER_JOINED;"
HOST_HELLO_GATE = "if (p->state == PEER_WAIT_HELLO && net.msgId == COOP_MSG_SESSION_HELLO) {"
HELLO_REFUSES = "if (diff & COOP_ID_DIFF_REFUSE) {"
HOST_GAME_GATE = "if (p->state == PEER_JOINED && net.msgId >= COOP_MSG_SESSION_END) {"
HOST_MESSAGE = ("Event_Init(ev, COOP_SESSION_EV_MESSAGE, net.peer);\n                    ev->msgId = net.msgId;\n"
                "                    ev->size = net.size;\n                    ev->data = net.data;")
WELCOME_PLAYER = "if (net->size < WELCOME_HEAD || net->data[0] < 1 || net->data[0] > COOP_NET_MAX_PEERS ||"
WELCOME_READ = "!Identity_ReadAll(host, net->data + WELCOME_HEAD, (uint16_t) (net->size - WELCOME_HEAD))) {"
REFUSE_READ = "if (net->size < 4 || !Identity_ReadAll(host, net->data + 4, (uint16_t) (net->size - 4))) {"
REASONS = ("ev->diff = (uint32_t) net->data[0] | ((uint32_t) net->data[1] << 8) | ((uint32_t) net->data[2] << 16) |\n"
           "               ((uint32_t) net->data[3] << 24);")
CLIENT_TIMEOUT = "if (s->client == CLIENT_WAIT_ANSWER && nowMs - s->clientSince >= COOP_SESSION_HANDSHAKE_MS) {"
CLIENT_WAIT = "s->client = CLIENT_WAIT_ANSWER;\n                s->clientSince = nowMs;"
CLIENT_ANSWER_GATE = ("if (s->client == CLIENT_WAIT_ANSWER &&\n"
                      "                    (net.msgId == COOP_MSG_SESSION_WELCOME || "
                      "net.msgId == COOP_MSG_SESSION_REFUSE)) {")
CLIENT_GAME_GATE = "if (s->client == CLIENT_ACTIVE && net.msgId >= COOP_MSG_SESSION_END) {"
CLIENT_MESSAGE = ("Event_Init(ev, COOP_SESSION_EV_MESSAGE, 0);\n                    ev->msgId = net.msgId;\n"
                  "                    ev->size = net.size;\n                    ev->data = net.data;")
PICK_GUARD = ("if (s->client != CLIENT_ACTIVE || s->started || character <= COOP_CHARACTER_FOX ||\n"
              "        character >= COOP_CHARACTER_COUNT) {")
PICK_SEND = "return s->t->ops->send(s->t, 0, COOP_MSG_SESSION_PICK, &wire, 1, COOP_NET_RELIABLE);"
START_GUARD = "if (!s->isHost || s->started) {"
START_LEVEL = "if (!CoopSession_CanStart(s) || start->level >= COOP_LEVEL_START_LEVELS) {"
START_WAIT = ("if (s->peers[i].state == PEER_WAIT_HELLO || s->roster.character[i + 1] == "
              "COOP_CHARACTER_UNPICKED) {")
START_LOOP = "for (i = 0; i < COOP_NET_MAX_PEERS; i++) {\n        " + START_WAIT
LEVEL_SEND = ("if (s->t->ops->send(s->t, i, COOP_MSG_SESSION_LEVEL_START, s->buffer, LEVEL_START_SIZE,\n"
              "                                COOP_NET_RELIABLE)) {")
LEVEL_HEAD = ("if (size != LEVEL_START_SIZE || data[0] >= COOP_LEVEL_START_LEVELS || data[2] == 0 ||\n"
              "        !Roster_Valid(roster, COOP_SESSION_PLAYERS, self)) {")
LEVEL_UNPICKED = ("for (i = 1; i < COOP_SESSION_PLAYERS; i++) {\n        if (roster[i] == COOP_CHARACTER_UNPICKED) {")
LEVEL_GATE = "if (s->client == CLIENT_ACTIVE && Client_Open(s) && net.msgId == COOP_MSG_SESSION_LEVEL_START) {"
SAVE_GATE = ("if (s->client == CLIENT_ACTIVE && Client_Open(s) && !s->saveHeld &&\n"
             "                    net.msgId == COOP_MSG_SESSION_SAVE) {")
SAVE_SEND = ("if (p->saveDue && !p->recoverDue &&\n            s->t->ops->send(s->t, i, COOP_MSG_SESSION_SAVE, s->level.save, "
             "COOP_SAVE_SIZE, COOP_NET_RELIABLE)) {")
WRITE_VALUE = ("*out++ = (uint8_t) value;\n        *out++ = (uint8_t) (value >> 8);\n"
               "        *out++ = (uint8_t) (value >> 16);\n        *out++ = (uint8_t) (value >> 24);")
READ_VALUE = ("uint32_t value =\n            (uint32_t) data[0] | ((uint32_t) data[1] << 8) | "
              "((uint32_t) data[2] << 16) | ((uint32_t) data[3] << 24);")
CLIENT_ROSTER = ("return (s->client == CLIENT_ACTIVE && s->roster.character[0] == COOP_CHARACTER_FOX) ? "
                 "&s->roster : NULL;")
CHARACTER_READ = "return net->size == 1 && net->data[0] >= COOP_CHARACTER_FOX && net->data[0] < COOP_CHARACTER_COUNT;"
OWED_REFUSAL = ("if (p->refusedPick != 0 &&\n"
                "            s->t->ops->send(s->t, i, COOP_MSG_SESSION_PICK_REFUSE, &p->refusedPick, 1, "
                "COOP_NET_RELIABLE)) {\n            p->refusedPick = 0;\n        }")
OWED_ROSTER = ("if (p->rosterDue && s->t->ops->send(s->t, i, COOP_MSG_SESSION_ROSTER, s->roster.character,\n"
               "                                            sizeof(s->roster.character), COOP_NET_RELIABLE)) {\n"
               "            p->rosterDue = false;\n        }")
HOST_PICK_GATE = "if (p->state == PEER_JOINED && !s->started && net.msgId == COOP_MSG_SESSION_PICK) {"
BAD_PICK = ("s->t->ops->disconnect(s->t, peer);\n        p->state = PEER_FREE;\n"
            "        Host_Left(s, peer, COOP_SESSION_END_BAD_MESSAGE, ev);\n        return true;")
TAKEN_LOOP = "for (i = 0; i < COOP_SESSION_PLAYERS; i++) {\n        if (s->roster.character[i] == net->data[0]) {"
PICK_TAKEN = "Host_RosterChanged(s);\n    return Host_RosterEvent(s, ev);"
OWN_PICK = "if (s->roster.character[peer + 1] == net->data[0]) {\n        return false;\n    }\n"
CLIENT_BROKEN = "s->t->ops->disconnect(s->t, 0);\n    Client_End(s, COOP_SESSION_END_BAD_MESSAGE, ev);\n}"
ROSTER_HEAD = ("if (size != COOP_SESSION_PLAYERS || data[0] != COOP_CHARACTER_FOX || "
               "data[self] == COOP_CHARACTER_NO_PLAYER) {")
ROSTER_VALUE = "if (data[i] == COOP_CHARACTER_FOX || data[i] >= COOP_CHARACTER_COUNT) {"
ROSTER_TWICE = "if (data[i] > COOP_CHARACTER_FOX && data[i] == data[j]) {"
ROSTER_GATE = "if (s->client == CLIENT_ACTIVE && net.msgId == COOP_MSG_SESSION_ROSTER) {"
PICK_REFUSE_GATE = "if (s->client == CLIENT_ACTIVE && net.msgId == COOP_MSG_SESSION_PICK_REFUSE) {"
HOST_MESSAGE_ID = "                    ev->msgId = net.msgId;\n"
MESSAGE_SIZE = "                    ev->size = net.size;\n"
MESSAGE_DATA = "                    ev->data = net.data;"
REJOIN_ID = "} else if (!s->started || s->id == 0 || Id_Read(net->data) != s->id) {"
AWAY = "bool away = s->started && end == COOP_SESSION_END_TIMEOUT && s->id != 0;"
AWAY_OVER = "if (s->away[i].on && nowMs - s->away[i].since >= COOP_SESSION_REJOIN_MS) {"
RESTART_RUN = "s->level.run = (run != 0) ? run : 1;"
RECOVER_CALL = "if (!s->started || reason < COOP_RECOVER_LOST || reason >= COOP_RECOVER_REASONS) {"
RECOVER_GATE = "if (s->client == CLIENT_ACTIVE && s->started && net.msgId == COOP_MSG_SESSION_RECOVER) {"
RECOVER_READ = ("if (net->size != 2 || net->data[0] < COOP_RECOVER_LOST || net->data[0] >= COOP_RECOVER_REASONS ||")
AWAIT_OVER = "if (s->awaiting && nowMs - s->awaitSince >= COOP_SESSION_RECOVER_MS) {"
SIM = ["sim"]

# name, text in session.c (exactly once), replacement, scenarios that must fail. Real-process scenarios only where the
# run ends quickly with the break in: a handshake that never finishes costs its full 60 s.
BREAKS = [
    # Creating, closing, asking.
    ("no_null_transport_check", "if (transport == NULL) {\n        return NULL;\n    }\n", "", SIM),
    ("close_null_session", "if (s == NULL) {\n        return;\n    }\n    CoopSession_SayClosed(s);\n    "
     "CoopLog_End(s->nowMs, Log_Send, s);\n    s->t->ops->close(s->t);",
     "CoopSession_SayClosed(s);\n    CoopLog_End(s->nowMs, Log_Send, s);\n    s->t->ops->close(s->t);", SIM),
    ("close_keeps_transport", "s->t->ops->close(s->t);\n    free(s);", "free(s);", SIM),
    ("identity_host_any_state", "return s->peers[peer].state == PEER_JOINED ? &s->peers[peer].identity : NULL;",
     "return &s->peers[peer].identity;", SIM),
    ("identity_client_any_state", CLIENT_IDENTITY, "return peer == 0 ? &s->peers[0].identity : NULL;", SIM),
    ("identity_client_any_peer", CLIENT_IDENTITY, "return s->client == CLIENT_ACTIVE ? &s->peers[0].identity : NULL;",
     SIM),

    # Counters: only for a joined peer, and the transport's own.
    ("stats_any_peer", "if (CoopSession_PeerIdentity(s, peer) == NULL) {\n        return false;\n    }\n"
     "    return s->t->ops->stats(s->t, peer, out);", "return s->t->ops->stats(s->t, peer, out);", SIM),
    ("stats_not_passed_on", "return s->t->ops->stats(s->t, peer, out);", "return true;", SIM),

    # Sending: only game ids, only to a joined peer.
    ("send_session_ids", SEND_GUARD, "if (CoopSession_PeerIdentity(s, peer) == NULL) {", SIM),
    ("send_id_boundary", SEND_GUARD, SEND_GUARD.replace("<", "<="), SIM),
    ("send_before_join", SEND_GUARD, "if (msgId < COOP_MSG_SESSION_END) {", SIM),
    ("send_ignores_result", "return s->t->ops->send(s->t, peer, msgId, data, size, lane);",
     "s->t->ops->send(s->t, peer, msgId, data, size, lane);\n    return true;", SIM),

    # Endings.
    ("end_text_refused_missing", 'return "The host refused this game.";', 'return "";', SIM),
    ("end_timeout_as_closed", "return COOP_SESSION_END_TIMEOUT;", "return COOP_SESSION_END_CLOSED;", SIM),
    ("end_full_as_closed", "return COOP_SESSION_END_HOST_FULL;", "return COOP_SESSION_END_CLOSED;", SIM),
    ("end_version_as_closed", "return COOP_SESSION_END_PROTOCOL;", "return COOP_SESSION_END_CLOSED;", SIM),
    ("event_no_text", 'ev->text = "";', "ev->text = NULL;", SIM),
    ("event_not_cleared", "memset(ev, 0, sizeof(*ev));\n    ev->type = type;", "ev->type = type;", SIM),
    ("identity_empty_accepted", READ_ALL, "return read == size;", SIM),  # the bug of #55
    ("identity_trailing_accepted", READ_ALL, "return read != 0;", SIM),

    # Host: the refusal.
    ("reason_byte_1_unset", "s->buffer[1] = (uint8_t) (reasons >> 8);", "", SIM),
    ("reason_byte_2_unset", "s->buffer[2] = (uint8_t) (reasons >> 16);", "", SIM),
    ("reason_byte_3_unset", "s->buffer[3] = (uint8_t) (reasons >> 24);", "", SIM),
    ("refuse_write_unchecked", "if (size == 0 ||\n        !s->t->ops->send(s->t, peer, COOP_MSG_SESSION_REFUSE,",
     "if (!s->t->ops->send(s->t, peer, COOP_MSG_SESSION_REFUSE,", SIM),
    ("refuse_unreliable", "COOP_MSG_SESSION_REFUSE, s->buffer, (uint16_t) (size + 4), COOP_NET_RELIABLE)",
     "COOP_MSG_SESSION_REFUSE, s->buffer, (uint16_t) (size + 4), COOP_NET_UNRELIABLE)", SIM),
    ("refuse_failed_keeps_link", REFUSE_GONE, "p->state = PEER_FREE;\n    } else {", SIM),
    ("refuse_failed_keeps_state", REFUSE_GONE, "s->t->ops->disconnect(s->t, peer);\n    } else {", SIM),
    ("refused_not_kept", "p->state = PEER_REFUSED;\n        p->since = nowMs;", "p->state = PEER_FREE;", SIM),
    ("refused_since_unset", "p->state = PEER_REFUSED;\n        p->since = nowMs;", "p->state = PEER_REFUSED;", SIM),
    ("refused_event_wrong_peer", "Event_Init(ev, COOP_SESSION_EV_REFUSED, peer);",
     "Event_Init(ev, COOP_SESSION_EV_REFUSED, 0);", SIM),
    ("refused_event_no_reasons", "ev->diff = reasons;\n", "", SIM),
    ("refused_event_no_end", "ev->diff = reasons;\n    ev->end = COOP_SESSION_END_REFUSED;", "ev->diff = reasons;",
     SIM),
    ("host_text_malformed_lost", "if (reasons & COOP_SESSION_REFUSE_MALFORMED) {", "if (0) {", SIM),
    ("host_text_no_handshake_lost", "} else if (reasons == 0) {", "} else if (0) {", SIM),
    ("host_refusal_sides_swapped", "CoopIdentity_Describe(reasons, &s->local, &p->identity, s->text, sizeof(s->text));",
     "CoopIdentity_Describe(reasons, &p->identity, &s->local, s->text, sizeof(s->text));", SIM),
    ("host_refusal_no_text", "sizeof(s->text));\n    }\n    ev->text = s->text;\n}\n\n// True if the identity the peer",
     "sizeof(s->text));\n    }\n}\n\n// True if the identity the peer", SIM),

    # Host: the hello and the welcome.
    ("hello_not_read", "if (!Identity_ReadAll(&p->identity, net->data, net->size)) {",
     "Identity_ReadAll(&p->identity, net->data, net->size);\n    if (0) {", SIM),
    ("hello_not_compared", HELLO_REFUSES, "if (0) {", ["sim", "build"]),
    # A different platform only warns (decision 0010). This is the rule before it, from decision 0002: a refusal.
    ("platform_refuses", HELLO_REFUSES, "if (diff & COOP_ID_DIFF_ALL) {", ["sim", "platform"]),
    # A refusal names the platform too, when it differs as well.
    ("refusal_drops_platform", "Host_Refuse(s, peer, diff, nowMs, ev);",
     "Host_Refuse(s, peer, diff & ~COOP_ID_DIFF_PLATFORM, nowMs, ev);", ["sim", "platform_build"]),
    ("welcome_same_slot", "s->buffer[0] = (uint8_t) (peer + 1);", "s->buffer[0] = 1;", SIM),
    ("welcome_unreliable", "(uint16_t) (size + WELCOME_HEAD), COOP_NET_RELIABLE);",
     "(uint16_t) (size + WELCOME_HEAD), COOP_NET_UNRELIABLE);", SIM),
    ("welcome_failure_ignored",
     "Host_Refuse(s, peer, 0, nowMs, ev);\n        return;\n    }\n    p->state = PEER_JOINED;",
     "}\n    p->state = PEER_JOINED;", SIM),
    ("host_not_joined", "p->state = PEER_JOINED;\n", "", SIM),
    ("joined_event_wrong_peer", "Event_Init(ev, COOP_SESSION_EV_JOINED, peer);",
     "Event_Init(ev, COOP_SESSION_EV_JOINED, 0);", SIM),
    ("joined_event_same_slot", "ev->playerIndex = (uint8_t) (peer + 1);", "ev->playerIndex = 1;", SIM),
    ("joined_event_no_warning", "ev->diff = diff;\n", "", ["sim", "platform"]),
    ("joined_text_sides_swapped", "CoopIdentity_Describe(diff, &s->local, &p->identity, s->text, sizeof(s->text));",
     "CoopIdentity_Describe(diff, &p->identity, &s->local, s->text, sizeof(s->text));", ["sim", "platform"]),
    ("joined_no_text", "sizeof(s->text));\n    ev->text = s->text;\n}\n\n// Time-outs",
     "sizeof(s->text));\n}\n\n// Time-outs", ["sim", "platform"]),

    # Host: time.
    ("linger_no_wait", DELIVERED, "bool delivered = true;", SIM),
    ("linger_no_ack", DELIVERED, "bool delivered = false;", SIM),
    ("linger_forever", LINGER, "if (delivered) {", SIM),
    ("linger_off_by_one", LINGER, LINGER.replace(">=", ">"), SIM),
    ("linger_clock_wrap", LINGER, "if (delivered || nowMs >= p->since + COOP_SESSION_REFUSE_LINGER_MS) {", SIM),
    ("linger_no_goodbye", DROP_REFUSED, "p->state = PEER_FREE;", SIM),
    ("linger_keeps_state", DROP_REFUSED, "s->t->ops->disconnect(s->t, i);", SIM),
    ("linger_wrong_peer", DROP_REFUSED, DROP_REFUSED.replace("(s->t, i)", "(s->t, 0)"), SIM),
    ("host_no_timeout", HOST_TIMEOUT, "} else if (0) {", SIM),
    ("host_timeout_off_by_one", HOST_TIMEOUT, HOST_TIMEOUT.replace(">=", ">"), SIM),
    ("host_timeout_clock_wrap", HOST_TIMEOUT,
     "} else if (p->state == PEER_WAIT_HELLO && nowMs >= p->since + COOP_SESSION_HANDSHAKE_MS) {", SIM),
    ("host_timeout_any_state", HOST_TIMEOUT, "} else if (p->state != PEER_FREE && "
     "nowMs - p->since >= COOP_SESSION_HANDSHAKE_MS) {", SIM),
    ("host_timeout_event_lost", "Host_Refuse(s, i, 0, nowMs, ev);\n            return true;",
     "Host_Refuse(s, i, 0, nowMs, ev);\n            return false;", SIM),

    # Host: transport events.
    ("host_peer_unchecked",
     "if (net.peer < 0 || net.peer >= COOP_NET_MAX_PEERS) {\n            continue;\n        }\n", "", SIM),
    ("connected_no_state", CONNECTED_STATE, "p->since = nowMs;", SIM),
    ("connected_no_since", CONNECTED_STATE, "p->state = PEER_WAIT_HELLO;", SIM),
    ("left_for_anyone", PEER_JOINED_FLAG, "bool joined = true;", SIM),
    ("left_never", PEER_JOINED_FLAG, "bool joined = false;", SIM),
    ("left_keeps_state", "p->state = PEER_FREE;\n                if (joined) {", "if (joined) {", SIM),
    ("left_wrong_peer", "Event_Init(ev, COOP_SESSION_EV_LEFT, peer);", "Event_Init(ev, COOP_SESSION_EV_LEFT, 0);", SIM),
    ("left_wrong_end", "Host_Left(s, net.peer, End_FromNet(net.end), ev);",
     "Host_Left(s, net.peer, COOP_SESSION_END_CLOSED, ev);", SIM),
    ("left_no_text", "ev->text = CoopSession_EndText(ev->end);\n", "", SIM),
    ("second_hello_taken", HOST_HELLO_GATE, "if (net.msgId == COOP_MSG_SESSION_HELLO) {", SIM),
    ("any_message_is_hello", HOST_HELLO_GATE, "if (p->state == PEER_WAIT_HELLO) {", SIM),
    ("host_hold_back_off", HOST_GAME_GATE, "if (net.msgId >= COOP_MSG_SESSION_END) {", ["sim", "cvar"]),
    ("host_passes_session_ids", HOST_GAME_GATE, "if (p->state == PEER_JOINED) {", SIM),
    ("host_id_boundary", HOST_GAME_GATE, HOST_GAME_GATE.replace(">=", ">"), SIM),
    ("host_message_wrong_peer", HOST_MESSAGE, HOST_MESSAGE.replace("net.peer", "0"), SIM),
    ("host_message_no_id", HOST_MESSAGE, HOST_MESSAGE.replace(HOST_MESSAGE_ID, ""), SIM),
    ("host_message_no_size", HOST_MESSAGE, HOST_MESSAGE.replace(MESSAGE_SIZE, ""), SIM),
    ("host_message_no_data", HOST_MESSAGE, HOST_MESSAGE.replace(MESSAGE_DATA, ""), SIM),
    ("host_ignored_uncounted", "s->ignored++;\n                break;\n            default:\n                break;\n"
     "        }\n    }\n    return false;\n}\n\n// ---- Client ----",
     "break;\n            default:\n                break;\n        }\n    }\n    return false;\n}\n\n"
     "// ---- Client ----", SIM),

    # Client: the answer.
    ("client_end_not_final", "s->client = CLIENT_ENDED;\n    Event_Init(ev, COOP_SESSION_EV_ENDED, 0);",
     "Event_Init(ev, COOP_SESSION_EV_ENDED, 0);", SIM),
    ("client_end_no_text", "ev->end = end;\n    ev->text = CoopSession_EndText(end);", "ev->end = end;", SIM),
    ("welcome_size_unchecked", WELCOME_PLAYER, WELCOME_PLAYER.replace("net->size < WELCOME_HEAD || ", ""), SIM),
    ("welcome_player_0", WELCOME_PLAYER, WELCOME_PLAYER.replace("net->data[0] < 1 || ", ""), SIM),
    ("welcome_player_high", WELCOME_PLAYER, WELCOME_PLAYER.replace(" || net->data[0] > COOP_NET_MAX_PEERS", ""), SIM),
    ("welcome_player_off_by_one", WELCOME_PLAYER, WELCOME_PLAYER.replace("> COOP", ">= COOP"), SIM),
    ("welcome_not_read", WELCOME_READ, WELCOME_READ.replace("!Identity", "(Identity").replace("))) {", ")), 0)) {"),
     SIM),
    ("bad_welcome_stays",
     "s->t->ops->disconnect(s->t, 0);\n            Client_End(s, COOP_SESSION_END_BAD_MESSAGE, ev);\n"
     "            return;", "Client_End(s, COOP_SESSION_END_BAD_MESSAGE, ev);\n            return;", SIM),
    ("client_not_active", "s->client = CLIENT_ACTIVE;\n", "", SIM),
    ("client_event_same_slot", "ev->playerIndex = net->data[0];", "ev->playerIndex = 1;", SIM),
    ("client_not_compared", "ev->diff = CoopIdentity_Compare(host, &s->local);", "ev->diff = 0;",
     ["sim", "platform"]),
    ("client_warning_sides_swapped",
     "CoopIdentity_Describe(ev->diff, host, &s->local, s->text, sizeof(s->text));\n        ev->text = s->text;\n",
     "CoopIdentity_Describe(ev->diff, &s->local, host, s->text, sizeof(s->text));\n        ev->text = s->text;\n",
     ["sim", "platform"]),
    ("client_warning_no_text", "sizeof(s->text));\n        ev->text = s->text;\n        return;",
     "sizeof(s->text));\n        return;", ["sim", "platform"]),
    ("refused_client_stays", "// SESSION_REFUSE. We leave at once: the host waits for our goodbye or for the ack.\n"
     "    s->t->ops->disconnect(s->t, 0);\n", "", SIM),
    ("refuse_size_unchecked", REFUSE_READ, REFUSE_READ.replace("net->size < 4 || ", ""), SIM),
    ("refuse_not_read", REFUSE_READ,
     "if (net->size < 4 || (Identity_ReadAll(host, net->data + 4, (uint16_t) (net->size - 4)), 0)) {", SIM),
    ("refused_no_ended", "s->endPending = true;\n", "", SIM),
    ("refused_wrong_ending", "s->pendingEnd = COOP_SESSION_END_REFUSED;", "s->pendingEnd = COOP_SESSION_END_CLOSED;",
     SIM),
    ("client_refused_no_end", "Event_Init(ev, COOP_SESSION_EV_REFUSED, 0);\n    ev->end = COOP_SESSION_END_REFUSED;",
     "Event_Init(ev, COOP_SESSION_EV_REFUSED, 0);", SIM),
    ("reasons_byte_1_lost", REASONS, REASONS.replace(" | ((uint32_t) net->data[1] << 8)", ""), SIM),
    ("reasons_byte_2_lost", REASONS, REASONS.replace(" | ((uint32_t) net->data[2] << 16)", ""), SIM),
    ("reasons_byte_3_lost", REASONS, REASONS.replace(" |\n               ((uint32_t) net->data[3] << 24)", ""), SIM),
    ("client_text_malformed_lost", "if (ev->diff & COOP_SESSION_REFUSE_MALFORMED) {", "if (0) {", SIM),
    ("client_text_no_handshake_lost", "} else if ((ev->diff & COOP_ID_DIFF_ALL) == 0) {", "} else if (0) {", SIM),
    # A host with the old rule (decision 0002) refuses on the platform alone; this client still reads why.
    ("client_text_platform_lost", "} else if ((ev->diff & COOP_ID_DIFF_ALL) == 0) {",
     "} else if ((ev->diff & COOP_ID_DIFF_REFUSE) == 0) {", SIM),
    ("client_refusal_sides_swapped",
     "CoopIdentity_Describe(ev->diff, host, &s->local, s->text, sizeof(s->text));\n    }\n    ev->text = s->text;",
     "CoopIdentity_Describe(ev->diff, &s->local, host, s->text, sizeof(s->text));\n    }\n    ev->text = s->text;",
     SIM),

    ("client_refusal_no_text", "sizeof(s->text));\n    }\n    ev->text = s->text;\n}\n\nstatic bool Client_Poll(",
     "sizeof(s->text));\n    }\n}\n\nstatic bool Client_Poll(", SIM),

    # Client: time and transport events.
    ("pending_end_repeats", "s->endPending = false;\n", "", SIM),
    ("ended_not_final", "if (s->client == CLIENT_ENDED) {\n        return false;\n    }\n", "", SIM),
    ("client_no_timeout", CLIENT_TIMEOUT, "if (0) {", SIM),
    ("client_timeout_off_by_one", CLIENT_TIMEOUT, CLIENT_TIMEOUT.replace(">=", ">"), SIM),
    ("client_timeout_clock_wrap", CLIENT_TIMEOUT,
     "if (s->client == CLIENT_WAIT_ANSWER && nowMs >= s->clientSince + COOP_SESSION_HANDSHAKE_MS) {", SIM),
    ("client_timeout_connecting", CLIENT_TIMEOUT, CLIENT_TIMEOUT.replace("== CLIENT_WAIT_ANSWER", "!= CLIENT_ACTIVE"),
     SIM),
    ("client_timeout_any_state", CLIENT_TIMEOUT, "if (nowMs - s->clientSince >= COOP_SESSION_HANDSHAKE_MS) {", SIM),
    ("silent_host_stays", "s->t->ops->disconnect(s->t, 0);\n        Client_End(s, COOP_SESSION_END_NO_HANDSHAKE, ev);",
     "Client_End(s, COOP_SESSION_END_NO_HANDSHAKE, ev);", SIM),
    ("silent_host_wrong_end", "Client_End(s, COOP_SESSION_END_NO_HANDSHAKE, ev);",
     "Client_End(s, COOP_SESSION_END_CLOSED, ev);", SIM),
    ("hello_write_unchecked", "if (size == 0 || !s->t->ops->send(s->t, 0, s->rejoin ?",
     "if (!s->t->ops->send(s->t, 0, s->rejoin ?", SIM),
    ("hello_unreliable", "s->buffer, (uint16_t) (size + head), COOP_NET_RELIABLE)) {",
     "s->buffer, (uint16_t) (size + head), COOP_NET_UNRELIABLE)) {", SIM),
    ("hello_failure_stays",
     "s->t->ops->disconnect(s->t, 0);\n                    Client_End(s, COOP_SESSION_END_BAD_MESSAGE, ev);",
     "Client_End(s, COOP_SESSION_END_BAD_MESSAGE, ev);", SIM),
    ("client_never_waits", CLIENT_WAIT, "s->clientSince = nowMs;", SIM),
    ("client_since_unset", CLIENT_WAIT, "s->client = CLIENT_WAIT_ANSWER;", SIM),
    ("client_end_lost", "Client_End(s, End_FromNet(net.end), ev);\n                return true;",
     "Client_End(s, End_FromNet(net.end), ev);\n                break;", SIM),
    ("client_answer_any_state", CLIENT_ANSWER_GATE,
     "if (net.msgId == COOP_MSG_SESSION_WELCOME || net.msgId == COOP_MSG_SESSION_REFUSE) {", SIM),
    ("any_message_is_answer", CLIENT_ANSWER_GATE, "if (s->client == CLIENT_WAIT_ANSWER) {", SIM),
    ("client_hold_back_off", CLIENT_GAME_GATE, "if (net.msgId >= COOP_MSG_SESSION_END) {", ["sim", "cvar"]),
    ("client_passes_session_ids", CLIENT_GAME_GATE, "if (s->client == CLIENT_ACTIVE) {", SIM),
    ("client_id_boundary", CLIENT_GAME_GATE, CLIENT_GAME_GATE.replace(">=", ">"), SIM),
    ("client_message_no_id", CLIENT_MESSAGE, CLIENT_MESSAGE.replace(HOST_MESSAGE_ID, ""), SIM),
    ("client_message_no_size", CLIENT_MESSAGE, CLIENT_MESSAGE.replace(MESSAGE_SIZE, ""), SIM),
    ("client_message_no_data", CLIENT_MESSAGE, CLIENT_MESSAGE.replace(MESSAGE_DATA, ""), SIM),
    ("client_ignored_uncounted", "s->ignored++;\n                break;\n            default:\n                break;\n"
     "        }\n    }\n    return false;\n}\n\n// ---- The co-op log",
     "break;\n            default:\n                break;\n        }\n    }\n    return false;\n}\n\n"
     "// ---- The co-op log", SIM),

    # Characters: who may ask, and for what.
    ("host_is_not_fox", "s->roster.character[0] = COOP_CHARACTER_FOX;",
     "s->roster.character[0] = COOP_CHARACTER_FALCO;", SIM),
    ("host_null_unchecked", "if (s != NULL) {\n        s->roster.character[0] = COOP_CHARACTER_FOX;\n"
     "        CoopLog_Begin(true, 0);\n        Log_Identity(\"hosting a session; this game\", identity);\n    }",
     "s->roster.character[0] = COOP_CHARACTER_FOX;\n        CoopLog_Begin(true, 0);\n"
     "        Log_Identity(\"hosting a session; this game\", identity);", SIM),
    ("pick_before_welcome", PICK_GUARD, PICK_GUARD.replace("s->client != CLIENT_ACTIVE || ", ""), SIM),
    ("pick_fox", PICK_GUARD, PICK_GUARD.replace("character <= COOP", "character < COOP"), SIM),
    ("pick_no_low_bound", PICK_GUARD, PICK_GUARD.replace("character <= COOP_CHARACTER_FOX ||\n        ", ""), SIM),
    ("pick_no_high_bound", PICK_GUARD, PICK_GUARD.replace(" ||\n        character >= COOP_CHARACTER_COUNT", ""), SIM),
    ("pick_high_off_by_one", PICK_GUARD, PICK_GUARD.replace("character >= COOP", "character > COOP"), SIM),
    ("pick_after_level_start", PICK_GUARD, PICK_GUARD.replace("s->started || ", ""), SIM),
    ("pick_ignores_result", PICK_SEND, PICK_SEND.replace("return ", "") + "\n    return true;", SIM),
    ("pick_unreliable", PICK_SEND, PICK_SEND.replace("COOP_NET_RELIABLE", "COOP_NET_UNRELIABLE"), SIM),
    ("pick_wrong_id", PICK_SEND, PICK_SEND.replace("COOP_MSG_SESSION_PICK", "COOP_MSG_SESSION_ROSTER"), SIM),

    # Characters: what can be asked of the session.
    ("roster_host_none", "if (s->isHost) {\n        return &s->roster;\n    }\n", "", SIM),
    ("roster_after_the_end", CLIENT_ROSTER, CLIENT_ROSTER.replace("s->client == CLIENT_ACTIVE && ", ""), SIM),
    ("roster_before_the_first", CLIENT_ROSTER,
     CLIENT_ROSTER.replace(" && s->roster.character[0] == COOP_CHARACTER_FOX", ""), SIM),
    ("name_fox_lost", 'return "Fox";', 'return "";', SIM),
    ("name_falco_lost", 'return "Falco";', 'return "";', SIM),
    ("name_slippy_lost", 'return "Slippy";', 'return "";', SIM),
    ("name_peppy_lost", 'return "Peppy";', 'return "";', SIM),
    ("name_of_nothing", 'return "Peppy";\n        default:\n            return "";',
     'return "Peppy";\n        default:\n            return "Fox";', SIM),

    # Characters: reading a pick or a refusal, on either side.
    ("character_size_unchecked", CHARACTER_READ, CHARACTER_READ.replace("net->size == 1 && ", ""), SIM),
    ("character_trailing_accepted", CHARACTER_READ, CHARACTER_READ.replace("net->size == 1", "net->size >= 1"), SIM),
    ("character_no_low_bound", CHARACTER_READ, CHARACTER_READ.replace("net->data[0] >= COOP_CHARACTER_FOX && ", ""),
     SIM),
    ("character_low_off_by_one", CHARACTER_READ, CHARACTER_READ.replace("net->data[0] >= COOP", "net->data[0] > COOP"),
     SIM),
    ("character_no_high_bound", CHARACTER_READ, CHARACTER_READ.replace(" && net->data[0] < COOP_CHARACTER_COUNT", ""),
     SIM),
    ("character_high_off_by_one", CHARACTER_READ, CHARACTER_READ.replace("net->data[0] < COOP", "net->data[0] <= COOP"),
     SIM),
    ("refused_event_wrong_type", "Event_Init(ev, COOP_SESSION_EV_PICK_REFUSED, peer);",
     "Event_Init(ev, COOP_SESSION_EV_REFUSED, peer);", SIM),
    ("pick_refused_wrong_peer", "Event_Init(ev, COOP_SESSION_EV_PICK_REFUSED, peer);",
     "Event_Init(ev, COOP_SESSION_EV_PICK_REFUSED, 0);", SIM),
    ("pick_refused_no_character", "ev->character = character;\n", "", SIM),
    ("pick_refused_no_text", 'CoopSession_CharacterName(character));\n    ev->text = s->text;',
     "CoopSession_CharacterName(character));", SIM),
    ("pick_refused_no_name", 'snprintf(s->text, sizeof(s->text), "%s is already taken.", '
     "CoopSession_CharacterName(character));", 'snprintf(s->text, sizeof(s->text), "%s is already taken.", "Falco");',
     SIM),

    # Characters: the host's roster, and who hears of it.
    ("change_tells_one_peer", "s->peers[i].rosterDue = true;", "s->peers[0].rosterDue = true;", SIM),
    ("change_no_event", "s->rosterEvent = true;\n", "", SIM),
    ("roster_event_repeats", "s->rosterEvent = false;\n", "", SIM),
    ("owed_not_sent", "Host_SendOwed(s);\n    if (", "if (", SIM),
    ("owed_any_state", "if (p->state != PEER_JOINED) {\n            continue;\n        }\n", "", SIM),
    ("owed_refusal_repeats", OWED_REFUSAL, OWED_REFUSAL.replace("p->refusedPick = 0;\n", "\n"), SIM),
    ("owed_refusal_lost", OWED_REFUSAL,
     "if (p->refusedPick != 0) {\n            s->t->ops->send(s->t, i, COOP_MSG_SESSION_PICK_REFUSE, "
     "&p->refusedPick, 1, COOP_NET_RELIABLE);\n            p->refusedPick = 0;\n        }", SIM),
    ("owed_refusal_unreliable", OWED_REFUSAL, OWED_REFUSAL.replace("COOP_NET_RELIABLE", "COOP_NET_UNRELIABLE"), SIM),
    ("owed_refusal_wrong_peer", OWED_REFUSAL, OWED_REFUSAL.replace("(s->t, i,", "(s->t, 0,"), SIM),
    ("owed_roster_repeats", OWED_ROSTER, OWED_ROSTER.replace("p->rosterDue = false;\n", "\n"), SIM),
    ("owed_roster_lost", OWED_ROSTER,
     "if (p->rosterDue) {\n            s->t->ops->send(s->t, i, COOP_MSG_SESSION_ROSTER, s->roster.character, "
     "sizeof(s->roster.character), COOP_NET_RELIABLE);\n            p->rosterDue = false;\n        }", SIM),
    ("owed_roster_unreliable", OWED_ROSTER, OWED_ROSTER.replace("COOP_NET_RELIABLE", "COOP_NET_UNRELIABLE"), SIM),
    ("owed_roster_wrong_peer", OWED_ROSTER, OWED_ROSTER.replace("(s->t, i,", "(s->t, 0,"), SIM),
    ("owed_roster_cut", OWED_ROSTER, OWED_ROSTER.replace("sizeof(s->roster.character),", "3,"), SIM),
    ("left_keeps_character", "s->roster.character[peer + 1] = COOP_CHARACTER_NO_PLAYER;\n", "", SIM),
    ("left_wrong_slot", "s->roster.character[peer + 1] = COOP_CHARACTER_NO_PLAYER;",
     "s->roster.character[peer] = COOP_CHARACTER_NO_PLAYER;", SIM),
    ("left_no_roster", "s->roster.character[peer + 1] = COOP_CHARACTER_NO_PLAYER;\n        Host_RosterChanged(s);",
     "s->roster.character[peer + 1] = COOP_CHARACTER_NO_PLAYER;", SIM),
    ("joined_not_in_roster", "s->roster.character[peer + 1] = COOP_CHARACTER_UNPICKED;\n", "", SIM),
    ("joined_wrong_slot", "s->roster.character[peer + 1] = COOP_CHARACTER_UNPICKED;",
     "s->roster.character[peer] = COOP_CHARACTER_UNPICKED;", SIM),
    ("joined_holds_a_wingman", "s->roster.character[peer + 1] = COOP_CHARACTER_UNPICKED;",
     "s->roster.character[peer + 1] = COOP_CHARACTER_FALCO;", ["sim", "characters"]),
    ("joined_no_roster", "Host_RosterChanged(s);\n    Event_Init(ev, COOP_SESSION_EV_JOINED, peer);",
     "Event_Init(ev, COOP_SESSION_EV_JOINED, peer);", SIM),
    ("connected_keeps_owed", "memset(p, 0, sizeof(*p));\n                p->state = PEER_WAIT_HELLO;",
     "p->state = PEER_WAIT_HELLO;", SIM),

    # Characters: the host decides a pick.
    ("host_pick_any_state", HOST_PICK_GATE, "if (!s->started && net.msgId == COOP_MSG_SESSION_PICK) {", SIM),
    ("host_pick_off", HOST_PICK_GATE, "if (0) {", SIM),
    ("host_pick_event_lost", "if (Host_Pick(s, net.peer, &net, ev)) {\n                        return true;\n"
     "                    }\n", "Host_Pick(s, net.peer, &net, ev);\n", SIM),
    ("bad_pick_taken_for_a_pick", "if (!Character_Read(net)) {\n        s->t->ops->disconnect(s->t, peer);",
     "if (0) {\n        s->t->ops->disconnect(s->t, peer);", SIM),
    ("bad_pick_stays", BAD_PICK, BAD_PICK.replace("s->t->ops->disconnect(s->t, peer);\n        ", ""), SIM),
    ("bad_pick_keeps_state", BAD_PICK, BAD_PICK.replace("p->state = PEER_FREE;\n        ", ""), SIM),
    ("bad_pick_wrong_end", BAD_PICK, BAD_PICK.replace("COOP_SESSION_END_BAD_MESSAGE", "COOP_SESSION_END_CLOSED"), SIM),
    ("bad_pick_event_lost", BAD_PICK, BAD_PICK.replace("return true;", "return false;"), SIM),
    ("own_pick_refused", OWN_PICK, "", SIM),
    ("own_pick_told", OWN_PICK,
     "if (s->roster.character[peer + 1] == net->data[0]) {\n        Host_RosterChanged(s);\n"
     "        return Host_RosterEvent(s, ev);\n    }\n", SIM),
    ("taken_not_checked", "if (s->roster.character[i] == net->data[0]) {", "if (0) {",
     ["sim", "taken", "taken_bad_wifi"]),
    ("taken_skips_fox", TAKEN_LOOP, TAKEN_LOOP.replace("i = 0", "i = 1"), SIM),
    ("taken_skips_last", TAKEN_LOOP, TAKEN_LOOP.replace("i < COOP_SESSION_PLAYERS", "i < COOP_SESSION_PLAYERS - 1"),
     SIM),
    ("taken_not_told", "p->refusedPick = net->data[0];\n", "", SIM),
    ("taken_event_lost", "Event_PickRefused(s, peer, net->data[0], ev);\n            return true;",
     "Event_PickRefused(s, peer, net->data[0], ev);\n            return false;", SIM),
    ("taken_ends_client", "p->refusedPick = net->data[0];\n            Event_PickRefused(s, peer, net->data[0], ev);",
     "s->t->ops->disconnect(s->t, peer);\n            p->state = PEER_FREE;\n"
     "            Host_Left(s, peer, COOP_SESSION_END_CLOSED, ev);", SIM),
    ("taken_pick_goes_through", "Event_PickRefused(s, peer, net->data[0], ev);\n            return true;",
     "s->roster.character[peer + 1] = net->data[0];\n            Event_PickRefused(s, peer, net->data[0], ev);\n"
     "            return true;", SIM),
    ("pick_not_held", "s->roster.character[peer + 1] = net->data[0];\n    Host_RosterChanged(s);",
     "Host_RosterChanged(s);", SIM),
    ("pick_wrong_slot", "s->roster.character[peer + 1] = net->data[0];\n    Host_RosterChanged(s);",
     "s->roster.character[peer] = net->data[0];\n    Host_RosterChanged(s);", SIM),
    ("pick_no_roster", PICK_TAKEN, "return false;", SIM),
    ("pick_no_event", PICK_TAKEN, "Host_RosterChanged(s);\n    return false;", SIM),

    # Characters: what a client takes from the host.
    ("client_slot_unset", "s->player = net->data[0];\n", "", SIM),
    ("roster_trailing_accepted", ROSTER_HEAD, ROSTER_HEAD.replace("size != COOP", "size < COOP"), SIM),
    ("roster_no_fox", ROSTER_HEAD, ROSTER_HEAD.replace("data[0] != COOP_CHARACTER_FOX || ", ""), SIM),
    ("roster_without_us", ROSTER_HEAD, ROSTER_HEAD.replace(" || data[self] == COOP_CHARACTER_NO_PLAYER", ""), SIM),
    ("roster_second_fox", ROSTER_VALUE, ROSTER_VALUE.replace("data[i] == COOP_CHARACTER_FOX || ", ""), SIM),
    ("roster_no_high_bound", ROSTER_VALUE, ROSTER_VALUE.replace(" || data[i] >= COOP_CHARACTER_COUNT", ""), SIM),
    ("roster_high_off_by_one", ROSTER_VALUE, ROSTER_VALUE.replace("data[i] >= COOP", "data[i] > COOP"), SIM),
    ("roster_last_unchecked", "for (i = 1; i < COOP_SESSION_PLAYERS; i++) {\n        if (data[i] == COOP_CHARACTER_FOX",
     "for (i = 1; i < COOP_SESSION_PLAYERS - 1; i++) {\n        if (data[i] == COOP_CHARACTER_FOX", SIM),
    ("roster_twice_accepted", ROSTER_TWICE, "if (0) {", SIM),
    ("roster_unpicked_twice", ROSTER_TWICE, ROSTER_TWICE.replace("data[i] > COOP_CHARACTER_FOX && ", ""), SIM),
    ("roster_twice_from_second", "for (j = 1; j < i; j++) {", "for (j = 2; j < i; j++) {", SIM),
    ("roster_twice_not_next", "for (j = 1; j < i; j++) {", "for (j = 1; j < i - 1; j++) {", SIM),
    ("roster_not_read", "if (!Roster_Valid(net->data, net->size, s->player)) {", "if (0) {", SIM),
    ("roster_not_kept", "memcpy(s->roster.character, net->data, sizeof(s->roster.character));\n", "", SIM),
    ("roster_wrong_event", "Event_Init(ev, COOP_SESSION_EV_ROSTER, 0);\n}",
     "Event_Init(ev, COOP_SESSION_EV_LEFT, 0);\n}", SIM),
    ("broken_roster_stays", CLIENT_BROKEN, CLIENT_BROKEN.replace("s->t->ops->disconnect(s->t, 0);\n    ", ""), SIM),
    ("broken_roster_wrong_end", CLIENT_BROKEN, CLIENT_BROKEN.replace("END_BAD_MESSAGE", "END_CLOSED"), SIM),
    ("pick_refuse_not_read", "if (!Character_Read(net)) {\n        Client_Broken(s, ev);",
     "if (0) {\n        Client_Broken(s, ev);", SIM),
    ("client_roster_any_state", ROSTER_GATE, "if (net.msgId == COOP_MSG_SESSION_ROSTER) {", SIM),
    ("client_roster_off", ROSTER_GATE, "if (0) {", SIM),
    ("client_roster_lost", "Client_Roster(s, &net, ev);\n                    return true;",
     "Client_Roster(s, &net, ev);\n                    break;", SIM),
    ("client_pick_refuse_any_state", PICK_REFUSE_GATE, "if (net.msgId == COOP_MSG_SESSION_PICK_REFUSE) {", SIM),
    ("client_pick_refuse_off", PICK_REFUSE_GATE, "if (0) {", SIM),
    ("client_pick_refuse_lost", "Client_PickRefused(s, &net, ev);\n                    return true;",
     "Client_PickRefused(s, &net, ev);\n                    break;", SIM),

    # Level start: who may start, and when.
    ("start_on_a_client", START_GUARD, START_GUARD.replace("!s->isHost || ", ""), SIM),
    ("start_twice", START_GUARD, START_GUARD.replace(" || s->started", ""), SIM),
    ("start_asks_nobody", START_LEVEL, START_LEVEL.replace("!CoopSession_CanStart(s) || ", ""), SIM),
    ("start_any_level", START_LEVEL, START_LEVEL.replace(" || start->level >= COOP_LEVEL_START_LEVELS", ""), SIM),
    ("start_level_off_by_one", START_LEVEL, START_LEVEL.replace("start->level >= COOP", "start->level > COOP"), SIM),
    ("start_level_too_low", START_LEVEL, START_LEVEL.replace("LEVELS)", "LEVELS - 1)"), SIM),
    ("start_past_the_handshake", START_WAIT,
     "if (s->roster.character[i + 1] == COOP_CHARACTER_UNPICKED) {", SIM),
    ("start_past_the_unpicked", START_WAIT, "if (s->peers[i].state == PEER_WAIT_HELLO) {",
     ["sim", "unpicked_holds_start"]),
    ("start_wrong_slot", START_WAIT, START_WAIT.replace("character[i + 1]", "character[i]"), SIM),
    ("start_skips_first", START_LOOP, START_LOOP.replace("i = 0", "i = 1"), SIM),
    ("start_skips_last", START_LOOP, START_LOOP.replace("i < COOP_NET_MAX_PEERS", "i < COOP_NET_MAX_PEERS - 1"), SIM),
    ("start_not_kept", "s->level = *start;\n", "", ["sim", "level_start"]),
    ("start_roster_not_taken", "memcpy(s->level.roster, s->roster.character, sizeof(s->level.roster));\n", "", SIM),
    ("start_stays_open", "s->started = true;\n    s->levelEvent = true;", "s->levelEvent = true;", SIM),
    ("start_no_host_event", "s->started = true;\n    s->levelEvent = true;", "s->started = true;", SIM),
    ("start_nobody_told", "s->peers[i].levelDue = true;\n", "", ["sim", "level_start"]),

    # Level start: on the wire, out and in.
    ("level_write_wrong_level", "*out++ = start->level;", "*out++ = start->phase;", SIM),
    ("level_write_wrong_phase", "*out++ = start->phase;", "*out++ = start->level;", SIM),
    ("level_write_no_roster", "memcpy(out, start->roster, COOP_SESSION_PLAYERS);\n", "", SIM),
    ("level_write_byte_0", WRITE_VALUE, WRITE_VALUE.replace("(uint8_t) value;", "0;"), SIM),
    ("level_write_byte_1", WRITE_VALUE, WRITE_VALUE.replace("(value >> 8)", "0"), SIM),
    ("level_write_byte_2", WRITE_VALUE, WRITE_VALUE.replace("(value >> 16)", "0"), SIM),
    ("level_write_byte_3", WRITE_VALUE, WRITE_VALUE.replace("(value >> 24)", "0"), SIM),
    ("level_write_last_lost", "for (i = 0; i < COOP_LEVEL_START_VALUES; i++) {\n        uint32_t value = (uint32_t) "
     "start->values[i];", "for (i = 0; i < COOP_LEVEL_START_VALUES - 1; i++) {\n        uint32_t value = (uint32_t) "
     "start->values[i];", SIM),
    ("level_read_wrong_level", "start->level = *data++;", "start->level = 0; data++;", SIM),
    ("level_read_wrong_phase", "start->phase = *data++;", "start->phase = 0; data++;", SIM),
    ("level_read_no_roster", "memcpy(start->roster, data, COOP_SESSION_PLAYERS);\n", "", SIM),
    ("level_read_byte_0", READ_VALUE, READ_VALUE.replace("(uint32_t) data[0] | ", ""), SIM),
    ("level_read_byte_1", READ_VALUE, READ_VALUE.replace("((uint32_t) data[1] << 8) | ", ""), SIM),
    ("level_read_byte_2", READ_VALUE, READ_VALUE.replace("((uint32_t) data[2] << 16) |", "0 |"), SIM),
    ("level_read_byte_3", READ_VALUE, READ_VALUE.replace("((uint32_t) data[3] << 24)", "0"), SIM),
    ("level_read_values_lost", "start->values[i] = (int32_t) value;\n", "", SIM),
    ("level_size_short",
     "#define LEVEL_START_SIZE (LEVEL_START_HEAD + COOP_SESSION_PLAYERS + 4 * COOP_LEVEL_START_VALUES)",
     "#define LEVEL_START_SIZE (LEVEL_START_HEAD + COOP_SESSION_PLAYERS + 4 * COOP_LEVEL_START_VALUES - 1)", SIM),

    # The session's save (decision 0009 item 8): what the host owes ahead of the level start.
    ("save_nobody_told", "s->peers[i].saveDue = true;\n", "", SIM),
    ("save_not_sent", SAVE_SEND, "if (p->saveDue && false) {", SIM),
    ("save_unreliable", SAVE_SEND, SAVE_SEND.replace("COOP_NET_RELIABLE", "COOP_NET_UNRELIABLE"), SIM),
    ("save_wrong_id", SAVE_SEND, SAVE_SEND.replace("COOP_MSG_SESSION_SAVE", "COOP_MSG_SESSION_ROSTER"), SIM),
    ("save_wrong_bytes", SAVE_SEND, SAVE_SEND.replace("s->level.save", "s->buffer"), SIM),
    ("save_short", SAVE_SEND, SAVE_SEND.replace("COOP_SAVE_SIZE,", "COOP_SAVE_SIZE - 1,"), SIM),
    ("save_sent_forever", "            p->saveDue = false;\n", "", SIM),
    ("level_ahead_of_save", "if (p->levelDue && !p->saveDue) {", "if (p->levelDue) {", SIM),

    # Level start: what the host owes.
    ("level_stale_buffer", "LevelStart_Write(&s->level, s->buffer);\n", "", SIM),
    ("level_unreliable", LEVEL_SEND, LEVEL_SEND.replace("COOP_NET_RELIABLE", "COOP_NET_UNRELIABLE"), SIM),
    ("level_wrong_id", LEVEL_SEND, LEVEL_SEND.replace("COOP_MSG_SESSION_LEVEL_START", "COOP_MSG_SESSION_ROSTER"), SIM),
    ("level_sent_for_good", LEVEL_SEND + "\n                p->levelDue = false;\n            }",
     LEVEL_SEND + "\n            }\n            p->levelDue = false;", SIM),
    ("level_sent_forever", "                p->levelDue = false;\n", "", SIM),
    ("level_event_forever", "s->levelEvent = false;\n", "", SIM),
    ("level_event_off", "Host_RosterEvent(s, ev) || Host_LevelEvent(s, ev) || Host_Tick(s, nowMs, ev) ||",
     "Host_RosterEvent(s, ev) || Host_Tick(s, nowMs, ev) ||", SIM),
    ("level_event_no_start", "ev->levelStart = &s->level;\n", "", SIM),
    ("late_client_let_in", "if (s->started) {\n        Host_Refuse(s, peer, COOP_SESSION_REFUSE_STARTED, nowMs, ev);\n"
     "        return;\n    }\n", "", ["sim", "late_client"]),
    ("late_client_no_reason", "Host_Refuse(s, peer, COOP_SESSION_REFUSE_STARTED, nowMs, ev);",
     "Host_Refuse(s, peer, 0, nowMs, ev);", ["sim", "late_client"]),
    ("late_client_host_text", "} else if (reasons & COOP_SESSION_REFUSE_STARTED) {", "} else if (0) {", SIM),
    ("late_client_client_text", "} else if (ev->diff & COOP_SESSION_REFUSE_STARTED) {", "} else if (0) {",
     ["sim", "late_client"]),
    ("pick_open_after_level_start", HOST_PICK_GATE, HOST_PICK_GATE.replace("!s->started && ", ""), SIM),

    # Level start: what a client takes.
    ("client_level_any_state", LEVEL_GATE, LEVEL_GATE.replace("s->client == CLIENT_ACTIVE && ", ""), SIM),
    ("client_level_twice", LEVEL_GATE, LEVEL_GATE.replace("Client_Open(s) && ", ""), SIM),
    ("client_level_off", LEVEL_GATE, "if (0) {", ["sim", "level_start"]),
    ("client_level_lost", "Client_LevelStart(s, &net, ev);\n                    return true;",
     "Client_LevelStart(s, &net, ev);\n                    break;", SIM),
    ("client_level_not_read", "if (!s->saveHeld || !LevelStart_Valid(net->data, net->size, s->player) ||",
     "if (!s->saveHeld ||", SIM),
    ("client_level_without_save", "if (!s->saveHeld || !LevelStart_Valid(net->data, net->size, s->player) ||",
     "if (!LevelStart_Valid(net->data, net->size, s->player) ||", SIM),
    ("client_level_stays_open", "LevelStart_Read(&s->level, net->data);\n    s->started = true;",
     "LevelStart_Read(&s->level, net->data);", SIM),
    ("client_level_no_values", "LevelStart_Read(&s->level, net->data);\n", "", SIM),
    ("level_trailing_accepted", LEVEL_HEAD, LEVEL_HEAD.replace("size != LEVEL_START_SIZE", "size < LEVEL_START_SIZE"),
     SIM),
    ("level_any_level", LEVEL_HEAD, LEVEL_HEAD.replace(" || data[0] >= COOP_LEVEL_START_LEVELS", ""), SIM),
    ("level_level_off_by_one", LEVEL_HEAD, LEVEL_HEAD.replace("data[0] >= COOP", "data[0] > COOP"), SIM),
    ("level_level_too_low", LEVEL_HEAD, LEVEL_HEAD.replace("LEVELS ||", "LEVELS - 1 ||"), SIM),
    ("level_any_roster", LEVEL_HEAD, LEVEL_HEAD.replace(" ||\n        !Roster_Valid(roster, COOP_SESSION_PLAYERS, self)",
                                                       ""), SIM),
    ("level_wrong_roster_bytes", "const uint8_t* roster = data + LEVEL_START_HEAD;",
     "const uint8_t* roster = data + LEVEL_START_HEAD - 1;", SIM),
    ("level_unpicked_taken", "if (roster[i] == COOP_CHARACTER_UNPICKED) {", "if (0) {", SIM),
    ("level_unpicked_skips_first", LEVEL_UNPICKED, LEVEL_UNPICKED.replace("i = 1", "i = 2"), SIM),
    ("level_unpicked_skips_last", LEVEL_UNPICKED,
     LEVEL_UNPICKED.replace("i < COOP_SESSION_PLAYERS", "i < COOP_SESSION_PLAYERS - 1"), SIM),

    # The session's save: what a client takes.
    ("client_save_off", SAVE_GATE, "if (0) {", SIM),
    ("client_save_twice", SAVE_GATE, SAVE_GATE.replace(" && !s->saveHeld", ""), SIM),
    ("client_save_any_state", SAVE_GATE, SAVE_GATE.replace("s->client == CLIENT_ACTIVE && ", ""), SIM),
    ("client_save_any_size", "if (net->size != COOP_SAVE_SIZE) {", "if (0) {", SIM),
    ("client_save_lost", "memcpy(s->level.save, net->data, COOP_SAVE_SIZE);\n", "", SIM),
    ("client_save_not_held", "    s->saveHeld = true;\n", "", SIM),
    ("client_save_ends_poll", "if (Client_Save(s, &net, ev)) {\n                        return true;\n"
     "                    }\n                    break;", "Client_Save(s, &net, ev);\n                    return true;",
     SIM),
    # A message shorter than it must be is read past its end. Under SANITIZER_BREAKS until #121: a plain sim fails on
    # them too (roster_trailing, level_short), as the session also has to refuse the message.
    ("roster_read_past_end", ROSTER_HEAD, ROSTER_HEAD.replace("size != COOP_SESSION_PLAYERS", "size < 1"), SIM),
    ("level_read_past_end", LEVEL_HEAD, LEVEL_HEAD.replace("size != LEVEL_START_SIZE", "size < 1"), SIM),
    # ---- The session survives: the session id, a player who comes back, the recovery (session.h, "Recovery") ----
    ("welcome_no_id", "s->buffer[1 + i] = (uint8_t) (s->id >> (8 * i));", "s->buffer[1 + i] = 0;", SIM),
    ("client_id_not_kept", "s->id = Id_Read(net->data + 1);\n        s->client = CLIENT_ACTIVE;",
     "s->client = CLIENT_ACTIVE;", SIM),
    ("client_sets_id", "void CoopSession_SetId(CoopSession* s, uint64_t id) {\n    if (s->isHost) {",
     "void CoopSession_SetId(CoopSession* s, uint64_t id) {\n    if (1) {", SIM),
    ("rejoin_other_session_taken", "if (s->rejoin && Id_Read(net->data + 1) != s->id) {", "if (0) {", SIM),
    ("rejoin_sends_hello", "s->rejoin ? COOP_MSG_SESSION_REJOIN : COOP_MSG_SESSION_HELLO", "COOP_MSG_SESSION_HELLO",
     SIM),
    ("rejoin_no_character", "s->buffer[8] = s->rejoinCharacter;", "s->buffer[8] = 0;", SIM),
    ("rejoin_id_low_byte_only", "s->buffer[i] = (uint8_t) (s->id >> (8 * i));", "s->buffer[i] = (uint8_t) s->id;", SIM),
    ("rejoin_not_flagged", "ev->rejoined = s->rejoin;\n", "", SIM),
    ("rejoin_id_unchecked", REJOIN_ID, "} else if (!s->started || s->id == 0) {", SIM),
    ("rejoin_before_level", REJOIN_ID, "} else if (s->id == 0 || Id_Read(net->data) != s->id) {", SIM),
    ("rejoin_without_id", REJOIN_ID, "} else if (!s->started || Id_Read(net->data) != s->id) {", SIM),
    ("rejoin_identity_unchecked", "} else if (!Host_Fits(s, peer, nowMs, ev, &q.diff)) {", "} else if (0) {", SIM),
    ("rejoin_nobody_refused", "if (slot == 0 || !s->away[slot].on) {", "if (0) {", SIM),
    ("rejoin_stale_kept", "s->t->ops->disconnect(s->t, slot - 1);\n            s->peers[slot - 1].state = PEER_FREE;",
     "s->peers[slot - 1].state = PEER_FREE;", SIM),
    ("rejoin_stale_still_peer", "s->t->ops->disconnect(s->t, slot - 1);\n            s->peers[slot - 1].state = PEER_FREE;",
     "s->t->ops->disconnect(s->t, slot - 1);", SIM),
    ("rejoin_stale_unsaid", "Host_Queue(s, &left);\n            Host_RecoverBegin(s, COOP_RECOVER_LOST, (uint8_t) slot);",
     "Host_RecoverBegin(s, COOP_RECOVER_LOST, (uint8_t) slot);", SIM),
    ("rejoin_slot_not_moved", "s->roster.character[target] = character;\n        s->roster.character[slot] = other;",
     "s->roster.character[target] = character;", SIM),
    ("rejoin_away_not_moved", "s->away[slot] = kept;\n", "", SIM),
    ("rejoin_away_stays", "s->away[target].on = false;\n", "", SIM),
    ("rejoin_not_joined", "s->peers[peer].state = PEER_JOINED;\n", "", SIM),
    ("rejoin_event_not_flagged", "ev->rejoined = true;\n", "", SIM),
    ("rejoin_gate_any_state", "if (p->state == PEER_WAIT_HELLO && net.msgId == COOP_MSG_SESSION_REJOIN) {",
     "if (net.msgId == COOP_MSG_SESSION_REJOIN) {", SIM),
    ("refuse_unknown_host_text", "} else if (reasons & COOP_SESSION_REFUSE_UNKNOWN) {", "} else if (0) {", SIM),
    ("refuse_unknown_client_text", "} else if (ev->diff & COOP_SESSION_REFUSE_UNKNOWN) {", "} else if (0) {", SIM),
    ("away_never", AWAY, "bool away = false;", SIM),
    ("away_before_level", AWAY, AWAY.replace("s->started && ", ""), SIM),
    ("away_without_id", AWAY, AWAY.replace(" && s->id != 0", ""), SIM),
    ("away_on_goodbye", AWAY, AWAY.replace("end == COOP_SESSION_END_TIMEOUT && ", ""), SIM),
    ("away_since_unset", "s->away[peer + 1].since = s->nowMs;\n", "", SIM),
    ("away_flag_lost", "ev->away = away;\n", "", SIM),
    ("away_wait_off_by_one", AWAY_OVER, AWAY_OVER.replace(">=", ">"), SIM),
    ("away_wait_clock_wrap", AWAY_OVER,
     "if (s->away[i].on && nowMs >= s->away[i].since + COOP_SESSION_REJOIN_MS) {", SIM),
    ("away_wait_forever", AWAY_OVER, "if (0) {", SIM),
    ("away_over_keeps_character", "s->away[i].on = false;\n            s->roster.character[i] = COOP_CHARACTER_NO_PLAYER;",
     "s->away[i].on = false;", SIM),
    ("away_over_still_recovering", "s->recovering = Host_Joined(s) > 0 || Host_AnyAway(s);", "s->recovering = true;", SIM),
    ("away_over_wrong_peer", "Event_Init(ev, COOP_SESSION_EV_LEFT, i - 1);", "Event_Init(ev, COOP_SESSION_EV_LEFT, i);",
     SIM),
    ("away_handshake_not_waited", "if (s->peers[i].state == PEER_WAIT_HELLO) {\n            return false;",
     "if (0) {\n            return false;", SIM),
    ("recover_alone", "        if (Host_Joined(s) > 0 || Host_AnyAway(s)) {", "        if (1) {", SIM),
    ("recover_never_on_left", "        if (Host_Joined(s) > 0 || Host_AnyAway(s)) {", "        if (0) {", SIM),
    ("recover_reason_always_left", "Host_RecoverBegin(s, away ? COOP_RECOVER_LOST : COOP_RECOVER_LEFT, (uint8_t) (peer + 1));",
     "Host_RecoverBegin(s, COOP_RECOVER_LEFT, (uint8_t) (peer + 1));", SIM),
    ("recover_wrong_player", "Host_RecoverBegin(s, away ? COOP_RECOVER_LOST : COOP_RECOVER_LEFT, (uint8_t) (peer + 1));",
     "Host_RecoverBegin(s, away ? COOP_RECOVER_LOST : COOP_RECOVER_LEFT, (uint8_t) peer);", SIM),
    ("recover_nobody_told", "s->peers[i].recoverDue = s->peers[i].state == PEER_JOINED;",
     "s->peers[i].recoverDue = false;", SIM),
    ("recover_owner_not_told", "    Host_Queue(s, &q);\n}\n\n// A client that joined is gone.", "}\n\n// A client that joined is gone.",
     SIM),
    ("recover_flag_unset", "    s->recovering = true;\n    s->recoverReason = reason;", "    s->recoverReason = reason;", SIM),
    ("recover_sent_twice", "COOP_NET_RELIABLE)) {\n                p->recoverDue = false;", "COOP_NET_RELIABLE)) {", SIM),
    ("recover_unreliable", "COOP_MSG_SESSION_RECOVER, recover, sizeof(recover), COOP_NET_RELIABLE)",
     "COOP_MSG_SESSION_RECOVER, recover, sizeof(recover), COOP_NET_UNRELIABLE)", SIM),
    ("recover_fields_swapped", "uint8_t recover[2] = { s->recoverReason, s->recoverPlayer };",
     "uint8_t recover[2] = { s->recoverPlayer, s->recoverReason };", SIM),
    ("save_ahead_of_recover", "if (p->saveDue && !p->recoverDue &&", "if (p->saveDue &&", SIM),
    ("restart_keeps_run", RESTART_RUN, "s->level.run = 1;", SIM),
    ("restart_run_wraps_to_0", RESTART_RUN, "s->level.run = run;", SIM),
    ("restart_old_roster", "memcpy(s->level.roster, s->roster.character, COOP_SESSION_PLAYERS);\n", "", SIM),
    ("restart_not_from_restart_point", "    s->level = s->restart;\n", "", SIM),
    ("restart_nobody_told", "p->levelDue = true;\n    }\n}\n\n// While a recovery runs", "}\n}\n\n// While a recovery runs", SIM),
    ("restart_no_save", "p->saveDue = true;\n        p->levelDue = true;", "p->levelDue = true;", SIM),
    ("restart_no_event", "s->levelEvent = true;\n    s->recovering = false;", "s->recovering = false;", SIM),
    ("restart_stays_recovering", "s->levelEvent = true;\n    s->recovering = false;", "s->levelEvent = true;", SIM),
    ("restart_under_away", "    if (Host_AnyAway(s)) {\n        return false;\n    }\n    if (Host_Joined(s) == 0) {",
     "    if (Host_Joined(s) == 0) {", SIM),
    ("start_no_restart_point", "s->restart = s->level;\n    s->started = true;", "s->started = true;", SIM),
    ("start_run_0", "s->level.run = 1;\n    memcpy(", "s->level.run = 0;\n    memcpy(", SIM),
    ("set_restart_dropped", "    if (s->isHost && s->started) {\n        s->restart = *start;", "    if (0) {\n        s->restart = *start;",
     SIM),
    ("kick_alone_without_id", "(s->id == 0 && Host_Joined(s) < 2 && !Host_AnyAway(s))) {", "(0)) {", SIM),
    ("kick_before_level", "if (!s->isHost || !s->started || peer < 0 ||", "if (!s->isHost || peer < 0 ||", SIM),
    ("kick_no_goodbye", "slot = peer + 1;\n    s->t->ops->disconnect(s->t, peer);", "slot = peer + 1;", SIM),
    ("kick_still_peer", "s->t->ops->disconnect(s->t, peer);\n    s->peers[peer].state = PEER_FREE;\n    if (away) {",
     "s->t->ops->disconnect(s->t, peer);\n    if (away) {", SIM),
    ("kick_never_away", "    away = s->id != 0;", "    away = false;", SIM),
    ("kick_since_unset", "    if (away) {\n        s->away[slot].on = true;\n        s->away[slot].since = nowMs;",
     "    if (away) {\n        s->away[slot].on = true;", SIM),
    ("kick_keeps_character", "s->roster.character[slot] = COOP_CHARACTER_NO_PLAYER;\n", "", SIM),
    ("kick_unsaid", "left.away = away;\n    Host_Queue(s, &left);", "left.away = away;", SIM),
    ("kick_no_recovery", "Host_RecoverBegin(s, away ? COOP_RECOVER_LOST : COOP_RECOVER_LEFT, (uint8_t) slot);\n", "", SIM),
    ("recover_call_before_level", RECOVER_CALL, RECOVER_CALL.replace("!s->started || ", ""), SIM),
    ("recover_call_reason_0", RECOVER_CALL, RECOVER_CALL.replace("reason < COOP_RECOVER_LOST || ", ""), SIM),
    ("recover_call_reason_high", RECOVER_CALL, RECOVER_CALL.replace(" || reason >= COOP_RECOVER_REASONS", ""), SIM),
    ("recover_call_host_alone", "        if (Host_Joined(s) == 0 && !Host_AnyAway(s)) {\n            return false;",
     "        if (0) {\n            return false;", SIM),
    ("recover_call_names_someone", "Host_RecoverBegin(s, reason, 0xFF);", "Host_RecoverBegin(s, reason, 0);", SIM),
    ("recover_call_client_no_wait", "    if (!s->awaiting) {\n        s->awaiting = true;\n        s->awaitSince = nowMs;",
     "    if (!s->awaiting) {\n        s->awaitSince = nowMs;", SIM),
    ("recover_call_client_no_since", "    if (!s->awaiting) {\n        s->awaiting = true;\n        s->awaitSince = nowMs;",
     "    if (!s->awaiting) {\n        s->awaiting = true;", SIM),
    ("recover_call_client_no_event", "s->recoverReason = reason;\n    s->recoverEvent = true;", "s->recoverReason = reason;",
     SIM),
    ("recovering_client_never", "return s->isHost ? s->recovering : s->awaiting;", "return s->isHost ? s->recovering : false;",
     SIM),
    ("recover_event_no_reason", "ev->reason = reason;\n    ev->playerIndex = player;", "ev->playerIndex = player;", SIM),
    ("recover_event_no_player", "ev->reason = reason;\n    ev->playerIndex = player;", "ev->reason = reason;", SIM),
    ("recover_text_player_off_by_one", "(int) player + 1, COOP_SESSION_REJOIN_MS / 1000);",
     "(int) player, COOP_SESSION_REJOIN_MS / 1000);", SIM),
    ("recover_text_no_wait", "(int) player + 1, COOP_SESSION_REJOIN_MS / 1000);", "(int) player + 1, 0);", SIM),
    ("recover_text_left_as_parted", "        case COOP_RECOVER_LEFT:\n            snprintf(", "        case 99:\n            snprintf(",
     SIM),
    ("client_recover_before_level", RECOVER_GATE, RECOVER_GATE.replace("s->started && ", ""), SIM),
    ("client_recover_off", RECOVER_GATE, "if (0) {", SIM),
    ("client_recover_any_size", RECOVER_READ, RECOVER_READ.replace("net->size != 2 || ", "net->size < 2 || "), SIM),
    ("client_recover_reason_0", RECOVER_READ, RECOVER_READ.replace("net->data[0] < COOP_RECOVER_LOST || ", ""), SIM),
    ("client_recover_reason_high", RECOVER_READ, RECOVER_READ.replace(" || net->data[0] >= COOP_RECOVER_REASONS", ""),
     SIM),
    ("client_recover_any_player", "(net->data[1] >= COOP_SESSION_PLAYERS && net->data[1] != 0xFF)) {", "(0)) {", SIM),
    ("client_recover_door_shut", "    s->recoverHeard = true;\n    s->awaiting = true;", "    s->awaiting = true;", SIM),
    ("client_recover_no_wait", "    s->recoverHeard = true;\n    s->awaiting = true;", "    s->recoverHeard = true;", SIM),
    ("client_recover_wait_not_restarted", "s->awaitSince = nowMs;\n    Event_Recover(s, net->data[0], net->data[1], ev);",
     "Event_Recover(s, net->data[0], net->data[1], ev);", SIM),
    ("client_door_stays_open", "    s->recoverHeard = false;\n", "", SIM),
    ("client_wait_runs_on", "    s->recoverHeard = false;\n    s->awaiting = false;", "    s->recoverHeard = false;", SIM),
    ("client_save_kept", "    s->started = true;\n    s->saveHeld = false;", "    s->started = true;", SIM),
    ("client_run_unchecked", "(s->started && net->data[2] != (uint8_t) (s->level.run + 1) && "
     "!(s->level.run == 255 && net->data[2] == 1))) {", "(0)) {", SIM),
    ("client_run_no_wrap", " && !(s->level.run == 255 && net->data[2] == 1))) {", ")) {", SIM),
    ("client_first_run_checked", "(s->started && net->data[2] != (uint8_t) (s->level.run + 1) &&",
     "(net->data[2] != (uint8_t) (s->level.run + 1) &&", SIM),
    ("level_run_0_taken", LEVEL_HEAD, LEVEL_HEAD.replace(" || data[2] == 0", ""), SIM),
    ("level_run_not_written", "*out++ = start->run;", "*out++ = 0;", SIM),
    ("level_run_not_read", "start->run = *data++;", "data++;", SIM),
    ("client_wait_off_by_one", AWAIT_OVER, AWAIT_OVER.replace(">=", ">"), SIM),
    ("client_wait_clock_wrap", AWAIT_OVER, "if (s->awaiting && nowMs >= s->awaitSince + COOP_SESSION_RECOVER_MS) {", SIM),
    ("client_wait_forever", AWAIT_OVER, "if (0) {", SIM),
    ("client_wait_always", AWAIT_OVER, AWAIT_OVER.replace("s->awaiting && ", ""), SIM),
    ("client_wait_end_stays", "s->t->ops->disconnect(s->t, 0);\n        Client_End(s, COOP_SESSION_END_NO_RECOVERY, ev);",
     "Client_End(s, COOP_SESSION_END_NO_RECOVERY, ev);", SIM),
    ("end_text_no_recovery_missing", 'return "The host did not start the level again in time, so the session is over.";',
     'return "";', SIM),
    ("end_text_other_session_missing",
     'return "The host runs another session now, so this game cannot come back to its own.";', 'return "";', SIM),
]

# Texts the sanitizer breaks use more than once.
CLOSE = "s->t->ops->close(s->t);\n    free(s);"
REFUSE_TEXT = ("CoopIdentity_Describe(reasons, &s->local, &p->identity, s->text, sizeof(s->text));\n    }\n"
               "    ev->text = s->text;")
PROCESSES = [name for name in session_test.SCENARIOS if name not in SIM]  # the scenarios that start real programs

# Breaks that only a sanitizer can see: the program still behaves, it just touches memory it should not. These prove
# the --asan run in CI can fail. Run them with --asan.
SANITIZER_BREAKS = [
    ("close_leaks_session", CLOSE, "s->t->ops->close(s->t);", SIM),
    ("refuse_read_past_end", REFUSE_READ, REFUSE_READ.replace("net->size < 4", "net->size < 2"), SIM),
    # A rejoin shorter than its head: the identity is then read from before the payload's end, and refused all the same.
    ("rejoin_short_read", "if (net->size < REJOIN_HEAD ||\n        !Identity_ReadAll(&s->peers[peer].identity,",
     "if (!Identity_ReadAll(&s->peers[peer].identity,", SIM),
    ("identity_peer_unchecked", "if (peer < 0 || peer >= COOP_NET_MAX_PEERS) {\n        return NULL;\n    }\n", "",
     SIM),
    # One side alone, at its very end: its result file is written and looks right, so only its exit code can tell.
    # session_test.py has no log scan like net_test.py --asan because it reads every exit (#92). A scenario that stops
    # reading its host's exit, or that of all its clients, lets one of these two survive.
    ("host_close_leaks", CLOSE, "s->t->ops->close(s->t);\n    if (!s->isHost) {\n        free(s);\n    }", PROCESSES),
    ("client_close_leaks", CLOSE, "s->t->ops->close(s->t);\n    if (s->isHost) {\n        free(s);\n    }", PROCESSES),
    # net_mutants.py's refuse_overruns_tx_packet for the session: after the refusal went out, so the client is content.
    # Past the allocation, not past s->text: that is the last field, and the padding after it is still inside the
    # allocation, so s->text[sizeof(s->text)] is a change no sanitizer sees.
    ("refuse_reads_past_session", REFUSE_TEXT,
     REFUSE_TEXT.replace("ev->text", "(void) ((volatile uint8_t*) s)[sizeof(*s)];\n    ev->text"),
     ["build", "rom", "cvar", "two_clients", "refuse_hell"]),
]


def options(ap):
    ap.add_argument("--asan", action="store_true", help="the breaks only AddressSanitizer and UBSan can see")


def table(args):
    return mutants.Table(
        source=ROOT / "src" / "port" / "coop" / "session" / "session.c",
        test_script=ROOT / "tools" / "coop" / "session_test.py",
        test_args=["--asan"] if args.asan else [],
        scenarios=session_test.SCENARIOS,
        target=session_test.target(args.asan),
        find_exe=lambda build_dir, config: session_test.find_exe(build_dir, config, args.asan),
        breaks=SANITIZER_BREAKS if args.asan else BREAKS,
        known=BREAKS + SANITIZER_BREAKS,
    )


if __name__ == "__main__":
    sys.exit(mutants.main(__doc__.split("\n\n")[0], table, options))
