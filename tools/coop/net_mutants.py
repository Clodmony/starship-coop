#!/usr/bin/env python3
"""Mutation check for the co-op transport: break one behaviour of net_core.c, rebuild, require its tests to fail.

    python tools/coop/net_mutants.py                 # every break; about 10 minutes
    python tools/coop/net_mutants.py --only no_resend,no_goodbye
    python tools/coop/net_mutants.py --list
    python tools/coop/net_mutants.py --check         # only check that every pattern still matches; no build
    python tools/coop/net_mutants.py --asan          # the breaks only the sanitizers can see; not on Windows

A test suite that passes proves little until you know it fails when the code is wrong. Each break below replaces one
piece of text in src/port/coop/net/net_core.c with a broken variant and names the net_test.py scenarios that must
fail with it. How a run goes, and how it guards the source it changes: tools/coop/mutants.py.

The breaks come from the check done by hand for #42 (16 breaks, 20 break-and-test pairs). Add one whenever you fix a
bug in net_core.c: the old, broken text as the replacement, and the scenario that found it.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "coop"))
import mutants  # noqa: E402
import net_test  # noqa: E402  (scenario names and how the test program is found)

# name, text in net_core.c (exactly once), replacement, scenarios that must fail. Newlines are "\n" here; files
# checked out with CRLF are handled.
BREAKS = [
    ("stray_not_answered",
     "if (p->msgId != COOP_MSG_DISCONNECT) {\n        Core_Write(core, from, p->connId, COOP_MSG_DISCONNECT,",
     "if (0) {\n        Core_Write(core, from, p->connId, COOP_MSG_DISCONNECT,",
     ["sim"]),
    ("stray_goodbye_answered",
     "if (p->msgId != COOP_MSG_DISCONNECT) {\n        Core_Write(core, from, p->connId, COOP_MSG_DISCONNECT,",
     "if (1) {\n        Core_Write(core, from, p->connId, COOP_MSG_DISCONNECT,",
     ["sim"]),
    ("stray_answer_other_connection",
     "Core_Write(core, from, p->connId, COOP_MSG_DISCONNECT, 0, 0, 0, 0, NULL, 0);",
     "Core_Write(core, from, p->connId + 1, COOP_MSG_DISCONNECT, 0, 0, 0, 0, NULL, 0);",
     ["sim"]),
    ("stray_uncounted",
     "core->strayDatagrams++;\n    if (p->msgId != COOP_MSG_DISCONNECT) {",
     "if (p->msgId != COOP_MSG_DISCONNECT) {",
     ["sim"]),
    ("no_checksum",
     "if (Get32(data) != CoopNetCore_Crc32(data + 4, size - 4) || Get16(data + 4) != MAGIC) {",
     "if (Get16(data + 4) != MAGIC) {",
     ["sim", "corrupt"]),
    ("no_resend",
     "if (slot->used && now - slot->sentAt >= slot->rto) {",
     "if (0) {",
     ["sim", "bad_wifi"]),
    ("no_unreliable_dedupe",
     "if (Link_SeenUnreliable(l, p->seq)) {",
     "if (0) {",
     ["sim", "bad_wifi"]),
    ("reliable_as_unreliable",
     "if (p->flags & FLAG_RELIABLE) {",
     "if (0) {",
     ["sim"]),
    ("ack_frees_one_too_many",
     "for (seq = l->txBase; seq != ack; seq++) {",
     "for (seq = l->txBase; seq != (uint16_t) (ack + 1) && seq != l->txNext; seq++) {",
     ["sim"]),
    ("ack_ignores_bits_bound",
     "if ((bits & (1u << i)) && (uint16_t) (seq - l->txBase) < inFlight) {",
     "if (bits & (1u << (i ? i - 1 : 0))) {",
     ["sim"]),
    ("stale_rx_accepted",
     "if (ahead >= WINDOW) {",
     "if (ahead >= WINDOW && ahead < 0x8000) {",
     ["sim"]),
    ("ack_wrap_naive",
     "if (ahead > inFlight) {",
     "if (ack < l->txBase) {",
     ["sim"]),
    ("timeout_clock_wrap",
     "if (now - l->lastHeard > core->timeoutMs) {",
     "if (now > l->lastHeard + core->timeoutMs) {",
     ["sim"]),
    ("no_connect_retry",
     "l->nextConnect = now + CONNECT_RETRY_MS;",
     "l->nextConnect = now + 2000000000u;",
     ["sim"]),
    ("accept_not_resent",
     "// Our accept went missing.\n        l->lastHeard = now;\n"
     "        Link_Write(core, l, COOP_MSG_CONNECT_ACCEPT, 0, 0, NULL, 0);",
     "l->lastHeard = now;",
     ["sim"]),
    ("no_version_check",
     "if (p->version != COOP_PROTOCOL_VERSION) {\n        if (p->msgId == COOP_MSG_CONNECT_REQUEST) {",
     "if (0) {\n        if (p->msgId == COOP_MSG_CONNECT_REQUEST) {",
     ["sim"]),
    ("ack_never_sent",
     "if (l->ackDue) {",
     "if (0) {",
     ["sim"]),
    ("no_timeout",
     "if (now - l->lastHeard > core->timeoutMs) {",
     "if (0) {",
     ["blackout_timeout", "kill_client"]),
    ("no_goodbye",
     "for (i = 0; i < GOODBYE_COPIES; i++) {",
     "for (i = 0; i < 0; i++) {",
     ["clean_close"]),
    ("no_refuse_full",
     "Core_Refuse(core, from, p->connId, REFUSE_FULL);",
     "",
     ["host_full"]),
]

# Breaks that only a sanitizer can see: the program still behaves, it just touches memory it should not. These prove
# the --asan run in CI can fail. Run them with --asan.
SANITIZER_BREAKS = [
    # LeakSanitizer, at the normal end of a program.
    ("destroy_leaks_core", "CoopNetCore_Disconnect(core, i);\n    }\n    free(core);",
     "CoopNetCore_Disconnect(core, i);\n    }", ["sim", "lan"]),
    # UBSan. The bit is the same either way, so only the sanitizer can tell.
    ("ack_bit_signed_shift", "if ((bits & (1u << i)) && (uint16_t) (seq - l->txBase) < inFlight) {",
     "if ((bits & (1 << i)) && (uint16_t) (seq - l->txBase) < inFlight) {", ["sim", "lan"]),
    # AddressSanitizer, on the stack: only a message as large as the transport allows reaches the last byte.
    ("datagram_buffer_one_short", "uint8_t buffer[COOP_NET_MAX_DATAGRAM];",
     "uint8_t buffer[COOP_NET_MAX_DATAGRAM - 1];", ["sim", "lan"]),
    # AddressSanitizer, on the heap, in transport_sdlnet.c: its packet holds one byte less. This comes after the real
    # refusal, so the refused client and sc_host_full see nothing wrong, and sc_host_full never looks at its host. Only
    # the log scan of net_test.py --asan can fail it; that is what this break guards.
    ("refuse_overruns_tx_packet", "Core_Write(core, to, connId, COOP_MSG_CONNECT_REFUSE, 0, 0, 0, 0, &reason, 1);",
     "Core_Write(core, to, connId, COOP_MSG_CONNECT_REFUSE, 0, 0, 0, 0, &reason, 1);\n"
     "    core->tx(core->user, to, (const uint8_t*) core, COOP_NET_MAX_DATAGRAM + 1);", ["host_full"]),
]


def options(ap):
    ap.add_argument("--seeds", type=int, default=200, help="seeds for the sim scenario (passed to net_test.py)")
    ap.add_argument("--asan", action="store_true", help="the breaks only AddressSanitizer and UBSan can see")


def table(args):
    return mutants.Table(
        source=ROOT / "src" / "port" / "coop" / "net" / "net_core.c",
        test_script=ROOT / "tools" / "coop" / "net_test.py",
        test_args=["--seeds", str(args.seeds)] + (["--asan"] if args.asan else []),
        scenarios=net_test.SCENARIOS,
        target=net_test.target(args.asan),
        find_exe=lambda build_dir, config: net_test.find_exe(build_dir, config, args.asan),
        breaks=SANITIZER_BREAKS if args.asan else BREAKS,
        known=BREAKS + SANITIZER_BREAKS,
    )


if __name__ == "__main__":
    sys.exit(mutants.main(__doc__.split("\n\n")[0], table, options))
