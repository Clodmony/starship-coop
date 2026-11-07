#!/usr/bin/env python3
"""Mutation check for lockstep: break one behaviour of lockstep.c, rebuild, require its simulation to fail.

    python tools/coop/lockstep_mutants.py                # every break; under a minute
    python tools/coop/lockstep_mutants.py --only no_relay,gap_taken
    python tools/coop/lockstep_mutants.py --list
    python tools/coop/lockstep_mutants.py --check        # only check that every pattern still matches; no build

Each break below replaces one piece of text in src/port/coop/sync/lockstep.c with a broken variant and names the
lockstep_test.py scenario that must fail with it. How a run goes, and how it guards the source it changes:
tools/coop/mutants.py. Add a break whenever you fix a bug in lockstep.c.

Left out, because no test can tell them from the original:

- Link_Send: the cut at COOP_LOCKSTEP_BLOCK_FRAMES. Clients stay within the delay of each other, so nothing unconfirmed
  gets that long in a run; `malformed` covers the refusal of a longer block.
- Link_Send: the cut at COOP_LOCKSTEP_RING, for the same reason.
- CoopLockstep_Receive: `dirty` for the other clients. The next timed send passes the frames on all the same.
- CoopLockstep_Receive: `confirmed > from->next[player]`. Going backwards only repeats frames.
- CoopLockstep_Receive: `size < HEADER_SIZE`. Without it a short message is still refused, after a read past its end that
  only a sanitizer sees. The sim's messages sit in a larger buffer, so one would not see it here either.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "coop"))
import lockstep_test  # noqa: E402  (scenario names and how the test program is found)
import mutants  # noqa: E402

SIM = ["sim"]
BREAKS = [
    ("no_repeat", "if (link->open && (link->dirty || ((nowMs - link->sentMs) >= COOP_LOCKSTEP_RESEND_MS))) {",
     "if (link->open && link->dirty) {", SIM),
    ("no_relay", "return l->config.host ? Is_Present(l, player) : (player == l->config.self);",
     "return player == l->config.self;", SIM),
    ("delay_one_more", "l->next[config->self] = config->delay;", "l->next[config->self] = config->delay + 1;", SIM),
    ("hold_lost", "Pad_Put(&out[0], &local->hold);", "Pad_Put(&out[0], &local->press);", SIM),
    ("press_lost", "Pad_Put(&out[4], &local->press);", "Pad_Put(&out[4], &local->hold);", SIM),
    ("stick_mixed_up", "pad->stick_y = (int8_t) in[3];", "pad->stick_y = (int8_t) in[2];", SIM),
    ("button_cut", "pad->button = (uint16_t) (in[0] | (in[1] << 8));", "pad->button = (uint16_t) in[0];", SIM),
    ("no_confirm", "from->next[player] = confirmed;", "(void) confirmed;", SIM),
    ("gap_taken", "if (first > l->next[player]) {\n        return false;", "if (false) {\n        return false;", SIM),
    ("ring_overrun", "if ((first + count) > (l->frame + COOP_LOCKSTEP_RING)) {",
     "if ((first + count) > (l->frame + 2 * COOP_LOCKSTEP_RING)) {", SIM),
    ("client_speaks_for_others", "(l->config.host && (player != Link_Player(l, peer)))) {", "false) {", SIM),
    ("own_input_from_outside", "if (!Is_Present(l, player) || (player == l->config.self) ||",
     "if (!Is_Present(l, player) ||", SIM),
    ("absent_player_taken", "if (!Is_Present(l, player) || (player == l->config.self) ||",
     "if ((player == l->config.self) ||", SIM),
    ("waits_for_host_only", "if (Is_Present(l, i) && (l->next[i] <= l->frame)) {",
     "if (Is_Present(l, i) && (i == 0) && (l->next[i] <= l->frame)) {", SIM),
    ("waits_for_nobody", "if (Is_Present(l, i) && (l->next[i] <= l->frame)) {", "if (false) {", SIM),
    ("no_start_grace", "(l->frame == 0) ? COOP_LOCKSTEP_START_MS : COOP_LOCKSTEP_STALL_MS",
     "COOP_LOCKSTEP_STALL_MS", SIM),
    ("never_stalls", "return ((nowMs - l->progressMs) >= limit) ? CoopLockstep_Waiting(l) : -1;", "return -1;", SIM),
    ("frame_is_no_progress", "    l->frame++;\n    l->progressMs = nowMs;\n", "    l->frame++;\n", SIM),
    ("trailing_bytes_taken", "    if (in != end) {\n        return false;\n    }\n", "", SIM),
    ("empty_block_taken", "if ((count == 0) || (count > COOP_LOCKSTEP_BLOCK_FRAMES) ||",
     "if ((count > COOP_LOCKSTEP_BLOCK_FRAMES) ||", SIM),
    ("long_block_taken", "if ((count == 0) || (count > COOP_LOCKSTEP_BLOCK_FRAMES) ||", "if ((count == 0) ||", SIM),
    ("unused_link_taken", "|| !l->links[peer].open || (size < HEADER_SIZE)) {", "|| (size < HEADER_SIZE)) {", SIM),
]


def table(args):
    return mutants.Table(
        source=ROOT / "src" / "port" / "coop" / "sync" / "lockstep.c",
        test_script=ROOT / "tools" / "coop" / "lockstep_test.py",
        scenarios=lockstep_test.SCENARIOS,
        target="coop_lockstep_test",
        find_exe=lockstep_test.find_sim,
        breaks=BREAKS,
    )


if __name__ == "__main__":
    sys.exit(mutants.main(__doc__.split("\n\n")[0], table))
