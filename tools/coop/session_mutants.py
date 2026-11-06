#!/usr/bin/env python3
"""Mutation check for the co-op session: break one behaviour of session.c, rebuild, require its tests to fail.

    python tools/coop/session_mutants.py                 # every break; under a minute
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
- Host_Tick: dropping `memset(&p->identity, ...)` before a time-out's refusal, and Host_Poll: dropping
  `memset(p, 0, sizeof(*p))` on CONNECTED. The identity of a peer is only read in JOINED, and that state comes after
  CoopIdentity_Read has written all of it; the other fields are set right after the memset.
- Client_Answer: dropping `s->client = CLIENT_ENDED` before a refusal's ENDED. Poll returns the pending end first,
  and Client_End sets the same state.
- Host_Refuse: writing 0 instead of `reasons >> 16` and `reasons >> 24`. No reason bit above 8 exists yet. Dropping
  those lines is in the table.
- Session_New: the calloc failure. The test program has no way to make calloc fail.
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
HOST_GAME_GATE = "if (p->state == PEER_JOINED && net.msgId >= COOP_MSG_SESSION_END) {"
HOST_MESSAGE = ("Event_Init(ev, COOP_SESSION_EV_MESSAGE, net.peer);\n                    ev->msgId = net.msgId;\n"
                "                    ev->size = net.size;\n                    ev->data = net.data;")
WELCOME_PLAYER = "if (net->size < 1 || net->data[0] < 1 || net->data[0] > COOP_NET_MAX_PEERS ||"
WELCOME_READ = "!Identity_ReadAll(host, net->data + 1, (uint16_t) (net->size - 1))) {"
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
HOST_MESSAGE_ID = "                    ev->msgId = net.msgId;\n"
MESSAGE_SIZE = "                    ev->size = net.size;\n"
MESSAGE_DATA = "                    ev->data = net.data;"
SIM = ["sim"]

# name, text in session.c (exactly once), replacement, scenarios that must fail. Real-process scenarios only where the
# run ends quickly with the break in: a handshake that never finishes costs its full 60 s.
BREAKS = [
    # Creating, closing, asking.
    ("no_null_transport_check", "if (transport == NULL) {\n        return NULL;\n    }\n", "", SIM),
    ("close_null_session", "if (s == NULL) {\n        return;\n    }\n    s->t->ops->close(s->t);",
     "s->t->ops->close(s->t);", SIM),
    ("close_keeps_transport", "s->t->ops->close(s->t);\n    free(s);", "free(s);", SIM),
    ("identity_host_any_state", "return s->peers[peer].state == PEER_JOINED ? &s->peers[peer].identity : NULL;",
     "return &s->peers[peer].identity;", SIM),
    ("identity_client_any_state", CLIENT_IDENTITY, "return peer == 0 ? &s->peers[0].identity : NULL;", SIM),
    ("identity_client_any_peer", CLIENT_IDENTITY, "return s->client == CLIENT_ACTIVE ? &s->peers[0].identity : NULL;",
     SIM),

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
    ("host_refusal_no_text", "sizeof(s->text));\n    }\n    ev->text = s->text;\n}\n\nstatic void Host_Hello(",
     "sizeof(s->text));\n    }\n}\n\nstatic void Host_Hello(", SIM),

    # Host: the hello and the welcome.
    ("hello_not_read", "if (!Identity_ReadAll(&p->identity, net->data, net->size)) {",
     "Identity_ReadAll(&p->identity, net->data, net->size);\n    if (0) {", SIM),
    ("hello_not_compared", "if (diff & COOP_ID_DIFF_REFUSE) {", "if (0) {", ["sim", "build"]),
    ("platform_refuses", "if (diff & COOP_ID_DIFF_REFUSE) {", "if (diff) {", ["sim", "platform"]),
    ("refusal_drops_warning", "Host_Refuse(s, peer, diff, nowMs, ev);",
     "Host_Refuse(s, peer, diff & COOP_ID_DIFF_REFUSE, nowMs, ev);", SIM),
    ("welcome_same_slot", "s->buffer[0] = (uint8_t) (peer + 1);", "s->buffer[0] = 1;", SIM),
    ("welcome_unreliable", "COOP_MSG_SESSION_WELCOME, s->buffer, (uint16_t) (size + 1), COOP_NET_RELIABLE)",
     "COOP_MSG_SESSION_WELCOME, s->buffer, (uint16_t) (size + 1), COOP_NET_UNRELIABLE)", SIM),
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
    ("left_wrong_peer", "Event_Init(ev, COOP_SESSION_EV_LEFT, net.peer);", "Event_Init(ev, COOP_SESSION_EV_LEFT, 0);",
     SIM),
    ("left_wrong_end", "ev->end = End_FromNet(net.end);", "ev->end = COOP_SESSION_END_CLOSED;", SIM),
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
    ("welcome_size_unchecked", WELCOME_PLAYER, WELCOME_PLAYER.replace("net->size < 1 || ", ""), SIM),
    ("welcome_player_0", WELCOME_PLAYER, WELCOME_PLAYER.replace("net->data[0] < 1 || ", ""), SIM),
    ("welcome_player_high", WELCOME_PLAYER, WELCOME_PLAYER.replace(" || net->data[0] > COOP_NET_MAX_PEERS", ""), SIM),
    ("welcome_player_off_by_one", WELCOME_PLAYER, WELCOME_PLAYER.replace("> COOP", ">= COOP"), SIM),
    ("welcome_not_read", WELCOME_READ, "(Identity_ReadAll(host, net->data + 1, (uint16_t) (net->size - 1)), 0)) {",
     SIM),
    ("bad_welcome_stays",
     "s->t->ops->disconnect(s->t, 0);\n            Client_End(s, COOP_SESSION_END_BAD_MESSAGE, ev);\n"
     "            return;", "Client_End(s, COOP_SESSION_END_BAD_MESSAGE, ev);\n            return;", SIM),
    ("client_not_active", "s->client = CLIENT_ACTIVE;\n", "", SIM),
    ("client_event_same_slot", "ev->playerIndex = net->data[0];", "ev->playerIndex = 1;", SIM),
    ("client_not_compared", "ev->diff = CoopIdentity_Compare(host, &s->local);", "ev->diff = 0;", ["sim", "platform"]),
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
    ("client_text_no_handshake_lost", "} else if ((ev->diff & COOP_ID_DIFF_REFUSE) == 0) {", "} else if (0) {", SIM),
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
    ("hello_write_unchecked", "if (size == 0 ||\n                    !s->t->ops->send(s->t, 0, COOP_MSG_SESSION_HELLO,",
     "if (!s->t->ops->send(s->t, 0, COOP_MSG_SESSION_HELLO,", SIM),
    ("hello_unreliable", "COOP_MSG_SESSION_HELLO, s->buffer, size, COOP_NET_RELIABLE)",
     "COOP_MSG_SESSION_HELLO, s->buffer, size, COOP_NET_UNRELIABLE)", SIM),
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
     "        }\n    }\n    return false;\n}\n\nbool CoopSession_Poll",
     "break;\n            default:\n                break;\n        }\n    }\n    return false;\n}\n\n"
     "bool CoopSession_Poll", SIM),
]

# Breaks that only a sanitizer can see: the program still behaves, it just touches memory it should not. These prove
# the --asan run in CI can fail. Run them with --asan.
SANITIZER_BREAKS = [
    ("close_leaks_session", "s->t->ops->close(s->t);\n    free(s);", "s->t->ops->close(s->t);", SIM),
    ("refuse_read_past_end", REFUSE_READ, REFUSE_READ.replace("net->size < 4", "net->size < 2"), SIM),
    ("identity_peer_unchecked", "if (peer < 0 || peer >= COOP_NET_MAX_PEERS) {\n        return NULL;\n    }\n", "",
     SIM),
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
