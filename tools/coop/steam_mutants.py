#!/usr/bin/env python3
"""Mutation check for the Steam relay transport: break one behaviour of transport_steam.c, rebuild, require its tests to
fail.

    python tools/coop/steam_mutants.py                 # every break
    python tools/coop/steam_mutants.py --only no_gap_check,goodbye_unreliable
    python tools/coop/steam_mutants.py --list
    python tools/coop/steam_mutants.py --check         # only check that every pattern still matches; no build

A test suite that passes proves little until you know it fails when the code is wrong, and these tests run on a fake.
Each break below replaces one piece of text in src/port/coop/net/transport_steam.c with a broken variant and names the
steam_test.py scenarios that must fail with it. How a run goes, and how it guards the source it changes:
tools/coop/mutants.py.

Add a break whenever you fix a bug in transport_steam.c: the old, broken text as the replacement, and the scenario that
found it.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "coop"))
import mutants  # noqa: E402
import steam_test  # noqa: E402  (scenario names and how the test program is found)

# name, text in transport_steam.c (exactly once), replacement, scenarios that must fail. Newlines are "\n" here; files
# checked out with CRLF are handled.
BREAKS = [
    # Promises Steam does not keep for us.
    ("no_unreliable_dedupe",
     "if (Link_SeenUnreliable(l, f->seq)) {",
     "if (0) {",
     ["duplicates", "bad_wifi"]),
    ("reliable_duplicate_delivered",
     "    if (ahead == 0) {\n        l->rxNext++;",
     "    if (ahead == 0 || ahead >= 0x8000) {\n        l->rxNext++;",
     ["duplicates"]),
    ("no_gap_check",
     "    if (ahead < 0x8000) {\n        Link_End(l, COOP_NET_END_TIMEOUT);",
     "    if (ahead < 0x8000) {\n        l->rxNext = (uint16_t) (f->seq + 1);\n        return true;",
     ["gap"]),

    # What must keep its place on the reliable lane.
    ("accept_unreliable",
     "Link_Write(s, l, COOP_MSG_CONNECT_ACCEPT, COOP_NET_RELIABLE, 0, NULL, 0) == COOP_STEAM_BUSY;",
     "Link_Write(s, l, COOP_MSG_CONNECT_ACCEPT, COOP_NET_UNRELIABLE, 0, NULL, 0) == COOP_STEAM_BUSY;",
     ["early_traffic"]),
    ("goodbye_unreliable",
     "Link_Write(s, l, COOP_MSG_DISCONNECT, COOP_NET_RELIABLE, 0, NULL, 0);",
     "Link_Write(s, l, COOP_MSG_DISCONNECT, COOP_NET_UNRELIABLE, 0, NULL, 0);",
     ["clean_close"]),
    ("accept_not_retried",
     "    if (l->acceptDue) {\n        Link_SendAccept(s, l);\n    }",
     "",
     ["accept_busy"]),
    ("send_passes_the_accept",
     "if (l->state != LINK_CONNECTED || l->acceptDue) {",
     "if (l->state != LINK_CONNECTED) {",
     ["accept_busy"]),

    # Sessions.
    ("host_refuses_strangers",
     "if (s->isHost || l != NULL) {",
     "if (l != NULL) {",
     ["lan", "strangers"]),
    ("client_accepts_strangers",
     "if (s->isHost || l != NULL) {",
     "if (1) {",
     ["strangers"]),
    ("session_failed_ignored",
     "(l->state == LINK_CONNECTING || l->state == LINK_CONNECTED)) {\n            l->broken = true;",
     "(l->state == LINK_CONNECTING || l->state == LINK_CONNECTED)) {\n            l->broken = false;",
     ["session_failed"]),
    ("broken_session_not_closed",
     "        s->backend->ops->closeSession(s->backend, l->id);\n"
     "        Link_End(l, COOP_NET_END_TIMEOUT);\n        return;",
     "        Link_End(l, COOP_NET_END_TIMEOUT);\n        return;",
     ["session_failed", "broken_after_data"]),
    # The bug seed 24 of clean_close found: a send that met a broken session ended the link with messages still unread.
    ("broken_send_ends_at_once",
     "        l->broken = true;\n    }\n    return result;",
     "        l->broken = true;\n        Link_End(l, COOP_NET_END_TIMEOUT);\n    }\n    return result;",
     ["broken_after_data"]),
    ("broken_ends_before_drained",
     "if (l->broken && drained && (l->state == LINK_CONNECTING",
     "if (l->broken && (l->state == LINK_CONNECTING",
     ["failed_after_data"]),

    # Connecting.
    ("no_connect_retry",
     "l->nextConnect = now + CONNECT_RETRY_MS;",
     "l->nextConnect = now + 2000000000u;",
     ["restart"]),
    ("no_connect_limit",
     "if (now - l->startedAt > s->connectMs) {",
     "if (0) {",
     ["connect_timeout"]),
    ("connect_limit_too_short",
     "#define DEFAULT_CONNECT_MS 20000",
     "#define DEFAULT_CONNECT_MS 5000",
     ["slow_setup", "connect_timeout"]),
    ("no_version_check",
     "if (f->version != COOP_PROTOCOL_VERSION) {\n        if (f->msgId == COOP_MSG_CONNECT_REQUEST) {",
     "if (0) {\n        if (f->msgId == COOP_MSG_CONNECT_REQUEST) {",
     ["version"]),
    ("refuse_unreadable_across_versions",
     "if (f->msgId == COOP_MSG_CONNECT_REFUSE && f->size == 1) {",
     "if (f->version == COOP_PROTOCOL_VERSION && f->msgId == COOP_MSG_CONNECT_REFUSE && f->size == 1) {",
     ["version"]),
    ("no_refuse_full",
     "Steam_Refuse(s, from, f->connId, REFUSE_FULL);",
     "",
     ["host_full"]),
    ("connection_id_ignored",
     "if (l->connId != f->connId) {",
     "if (0) {",
     ["restart"]),
    ("restart_keeps_old_link",
     "if (f->msgId == COOP_MSG_CONNECT_REQUEST && l->state == LINK_CONNECTED) {\n"
     "            Link_End(l, COOP_NET_END_TIMEOUT);",
     "if (0) {\n            Link_End(l, COOP_NET_END_TIMEOUT);",
     ["restart"]),

    # Staying connected.
    ("no_timeout",
     "if (drained && now - l->lastHeard > s->timeoutMs) {",
     "if (0) {",
     ["blackout_timeout", "kill_client"]),
    ("timeout_ignores_waiting_frames",
     "if (drained && now - l->lastHeard > s->timeoutMs) {",
     "if (now - l->lastHeard > s->timeoutMs) {",
     ["stall"]),
    ("timeout_clock_wrap",
     "if (drained && now - l->lastHeard > s->timeoutMs) {",
     "if (drained && now > l->lastHeard + s->timeoutMs) {",
     ["sim"]),
    ("no_ping",
     "l->nextPing = now + s->pingMs;",
     "l->nextPing = now + 2000000000u;",
     ["quiet"]),
    ("no_goodbye",
     "Link_Write(s, l, COOP_MSG_DISCONNECT, COOP_NET_RELIABLE, 0, NULL, 0);",
     "",
     ["clean_close", "disconnect"]),

    # pending and the window.
    ("pending_always_zero",
     "out->pending = (uint16_t) (l->txNext - l->txAcked);",
     "out->pending = 0;",
     ["pending", "window"]),
    ("ack_never_sent",
     "    if (l->ackDue) {\n        Link_Write(s, l, COOP_MSG_ACK",
     "    if (0) {\n        Link_Write(s, l, COOP_MSG_ACK",
     ["window"]),
    ("ack_not_checked",
     "if ((uint16_t) (ack - l->txAcked) <= (uint16_t) (l->txNext - l->txAcked)) {",
     "if (1) {",
     ["acks"]),
    ("no_window",
     "if ((uint16_t) (l->txNext - l->txAcked) >= WINDOW ||\n            Link_Write",
     "if (Link_Write",
     ["window"]),
    ("sequence_lost_on_busy",
     "!= COOP_STEAM_SENT) {\n            return false;\n        }\n        l->txNext++;",
     "!= COOP_STEAM_SENT) {\n            l->txNext++;\n            return false;\n        }\n        l->txNext++;",
     ["send_buffer"]),

    # Frames and messages.
    ("no_magic_check",
     "size > HEADER_SIZE + COOP_NET_MAX_MESSAGE || Get16(data) != MAGIC) {",
     "size > HEADER_SIZE + COOP_NET_MAX_MESSAGE) {",
     ["bad_frames"]),
    ("no_size_check",
     "return f->size == size - HEADER_SIZE;",
     "return true;",
     ["bad_frames"]),
    ("no_limit_on_frame_size",
     "if (size < HEADER_SIZE || size > HEADER_SIZE + COOP_NET_MAX_MESSAGE || Get16(data) != MAGIC) {",
     "if (size < HEADER_SIZE || Get16(data) != MAGIC) {",
     ["bad_frames"]),
    ("message_released_too_early",
     "if (filled && ev->type == COOP_NET_EVENT_MESSAGE) {",
     "if (0) {",
     ["lan", "lifetime"]),
    ("message_never_released",
     "s->holding = true;",
     "s->holding = false;",
     ["lan", "lifetime"]),
    ("send_takes_oversize",
     "if (peer < 0 || peer >= s->maxLinks || size > COOP_NET_MAX_MESSAGE || msgId < COOP_MSG_TRANSPORT_END) {",
     "if (peer < 0 || peer >= s->maxLinks || msgId < COOP_MSG_TRANSPORT_END) {",
     ["limits"]),
    ("connects_to_itself",
     "if (backend != NULL && (host == 0 || host == backend->ops->self(backend))) {",
     "if (0) {",
     ["limits"]),
]


def options(ap):
    ap.add_argument("--seeds", default="20", help="seeds for the scenarios that take them (passed to steam_test.py)")


def table(args):
    return mutants.Table(
        source=ROOT / "src" / "port" / "coop" / "net" / "transport_steam.c",
        test_script=ROOT / "tools" / "coop" / "steam_test.py",
        test_args=["--seeds", str(args.seeds)],
        scenarios=steam_test.SCENARIOS,
        target="coop_steam_test",
        find_exe=steam_test.find_exe,
        breaks=BREAKS,
    )


if __name__ == "__main__":
    sys.exit(mutants.main(__doc__.split("\n\n")[0], table, options))
