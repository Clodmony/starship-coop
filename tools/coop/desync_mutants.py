#!/usr/bin/env python3
"""Mutation check for the desync check: break one behaviour of desync.c, rebuild, require its simulation to fail.

    python tools/coop/desync_mutants.py                # every break; under a minute
    python tools/coop/desync_mutants.py --only no_compare,wrong_frame
    python tools/coop/desync_mutants.py --list
    python tools/coop/desync_mutants.py --check        # only check that every pattern still matches; no build

Each break below replaces one piece of text in src/port/coop/sync/desync.c with a broken variant and names the
lockstep_test.py scenario that must fail with it. How a run goes, and how it guards the source it changes:
tools/coop/mutants.py. Add a break whenever you fix a bug in desync.c.

Left out, because no test can tell them from the original:

- Send_Batch: the total of a frame the ring no longer holds goes out as 0. A sender holds its own last period.
- CoopDesync_Done: `!d->sent`. Found sends in the same call, so a found desync is always sent.
- Receive_Hash: `size < COOP_DESYNC_HASH_HEADER`. Without it a short message is still refused by the count and size
  check, after a read past its end that only a sanitizer sees. The sim's messages sit in a larger buffer.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "coop"))
import lockstep_test  # noqa: E402  (scenario names and how the test program is found)
import mutants  # noqa: E402

SIM = ["desync_sim"]
BREAKS = [
    ("no_compare", "if ((e->hash.total != total) && !d->found) {", "if (false) {", SIM),
    ("compare_equal", "if ((e->hash.total != total) && !d->found) {", "if ((e->hash.total == total) && !d->found) {",
     SIM),
    ("wrong_frame", "Found(d, d->config.self, player, frame, player, nowMs);",
     "Found(d, d->config.self, player, frame + 1, player, nowMs);", SIM),
    ("old_frame_compared", "return (e->held && (e->frame == frame)) ? e : NULL;", "return e->held ? e : NULL;", SIM),
    ("unchecked_not_counted", "    if (e == NULL) {\n        d->unchecked++;\n        return;\n    }\n",
     "    if (e == NULL) {\n        return;\n    }\n", SIM),
    ("future_compared", "            if (frame >= d->frames) {\n                return;",
     "            if (false) {\n                return;", SIM),
    ("batch_not_sent", "    if (((frame + 1) % d->config.period) == 0) {\n        Send_Batch(d, frame);\n    }\n", "",
     SIM),
    ("batch_off_by_one", "uint32_t first = last + 1 - d->config.period;", "uint32_t first = last - d->config.period;",
     SIM),
    ("no_desync_sent", "    Send_All(d, COOP_MSG_SYNC_DESYNC, message, sizeof(message));\n    d->sent = true;\n",
     "    d->sent = true;\n", SIM),
    ("regions_not_sent", "Put64(&message[20 + 8 * i], d->mine.held ? d->mine.hash.region[i] : 0);",
     "Put64(&message[20 + 8 * i], 0);", SIM),
    ("pair_unsorted", "    d->pair[0] = (a < b) ? a : b;\n    d->pair[1] = (a < b) ? b : a;\n",
     "    d->pair[0] = a;\n    d->pair[1] = b;\n", SIM),
    ("answer_from_anyone", "if ((sender == CoopDesync_NeedFrom(d)) && !d->answered) {", "if (!d->answered) {", SIM),
    ("never_answered", "        d->answered = true;\n", "", SIM),
    ("no_grace", "    if ((nowMs - d->foundMs) >= COOP_DESYNC_GRACE_MS) {\n        return true;\n    }\n", "", SIM),
    ("done_at_once", "return d->answered && ((d->pair[0] == d->config.self) || (d->pair[1] == d->config.self));",
     "return true;", SIM),
    ("no_pair_wait", "return d->answered && ((d->pair[0] == d->config.self) || (d->pair[1] == d->config.self));",
     "return d->answered;", SIM),
    ("heard_not_found", "    if (!d->found) {\n        Found(d, a, b, frame, sender, nowMs);\n    }\n", "", SIM),
    ("regions_ignore_frame", "if (!d->mine.held || !d->theirs.held || (d->theirFrame != d->frame)) {\n        return 0;",
     "if (!d->mine.held || !d->theirs.held) {\n        return 0;", SIM),
    ("any_count_taken", "if ((count == 0) || (count > COOP_DESYNC_MAX_BATCH) || (size != COOP_DESYNC_HASH_HEADER + 8 * count)) {",
     "if (size != COOP_DESYNC_HASH_HEADER + 8 * count) {", SIM),
    ("any_size_taken", "if ((count == 0) || (count > COOP_DESYNC_MAX_BATCH) || (size != COOP_DESYNC_HASH_HEADER + 8 * count)) {",
     "if ((count == 0) || (count > COOP_DESYNC_MAX_BATCH) || (size < COOP_DESYNC_HASH_HEADER + 8 * count)) {", SIM),
    ("desync_size_taken", "    if (size != COOP_DESYNC_MESSAGE_SIZE) {\n        return false;\n    }\n",
     "    if (size < COOP_DESYNC_MESSAGE_SIZE) {\n        return false;\n    }\n", SIM),
    ("sender_not_checked", "if ((sender != Link_Player(d, peer)) || !Is_Present(d, a) || !Is_Present(d, b) || (a == b)) {",
     "if (!Is_Present(d, a) || !Is_Present(d, b) || (a == b)) {", SIM),
    ("pair_not_checked", "if ((sender != Link_Player(d, peer)) || !Is_Present(d, a) || !Is_Present(d, b) || (a == b)) {",
     "if (sender != Link_Player(d, peer)) {", SIM),
    ("unused_link_taken", "if ((peer < 0) || (peer >= COOP_NET_MAX_PEERS) || !d->links[peer].open) {",
     "if ((peer < 0) || (peer >= COOP_NET_MAX_PEERS)) {", SIM),
    ("pending_full_silent", "        d->unchecked += l->pending[0].count - l->pending[0].next;\n", "", SIM),
]


def table(args):
    return mutants.Table(
        source=ROOT / "src" / "port" / "coop" / "sync" / "desync.c",
        test_script=ROOT / "tools" / "coop" / "lockstep_test.py",
        scenarios=lockstep_test.SCENARIOS,
        target="coop_desync_test",
        find_exe=lockstep_test.find_desync_sim,
        breaks=BREAKS,
    )


if __name__ == "__main__":
    sys.exit(mutants.main(__doc__.split("\n\n")[0], table))
