#!/usr/bin/env python3
"""Tests of tools/coop/coop_log.py on made-up logs: parsing, the merge order, filters, offsets and lines held twice."""
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import coop_log  # noqa: E402

STAMP = "20261108-140322"


def write(folder, player, lines):
    (folder / f"coop-log-{STAMP}-player{player}.txt").write_text(
        f"# coop log {STAMP}: player {player}\n" + "".join(f"{line}\n" for line in lines), encoding="utf-8")


def main():
    checks = 0
    with tempfile.TemporaryDirectory() as tmp:
        host = Path(tmp) / "host"
        client = Path(tmp) / "client"
        host.mkdir()
        client.mkdir()
        p1 = ["2026-11-08 14:03:22.100 - - session hosting a session; this game: build abc",
              "2026-11-08 14:03:30.000 0 Corneria lockstep lockstep started at frame 0: input delay 3 frames",
              "2026-11-08 14:03:31.000 30 Corneria state player 1 active shields 255",
              "2026-11-08 14:03:31.500 45 Corneria desync frame 45: 40 frames compared with a peer, 0 unchecked"]
        p2 = ["2026-11-08 14:03:25.000 - - session joined as player 2",
              "2026-11-08 14:03:30.900 28 Corneria state player 2 active shields 255",
              "2026-11-08 14:03:31.200 36 Corneria lockstep frame 36 held 250 ms for player 1"]
        write(host, 1, p1)
        write(host, 2, p2[:2])  # the last line had not arrived when the host closed
        write(client, 2, p2)
        write(client, 1, p1[:3])

        line = coop_log.parse_line(p1[1], 1)
        assert line.frame == 0 and line.level == "Corneria" and line.category == "lockstep", vars(line)
        assert line.text.startswith("lockstep started"), line.text
        assert coop_log.parse_line("# header") is None
        assert coop_log.parse_line(p1[0]).frame is None
        checks += 4

        session, merged = coop_log.merge([host, client])
        assert session == STAMP, session
        assert len(merged) == len(p1) + len(p2), [m.format() for m in merged]  # each line once, both folders read
        times = [m.time for m in merged]
        assert times == sorted(times), times
        assert [m.player for m in merged][:2] == [1, 2], [m.format() for m in merged]
        checks += 4

        _, by_frame = coop_log.merge([host, client], by="frame")
        frames = [m.frame for m in by_frame if m.frame is not None]
        assert frames == sorted(frames) and by_frame[-1].frame is None, [m.format() for m in by_frame]
        checks += 1

        _, only = coop_log.merge([host], categories={"state"})
        assert {m.category for m in only} == {"state"} and len(only) == 2, [m.format() for m in only]
        _, only = coop_log.merge([host], players={2})
        assert {m.player for m in only} == {2} and len(only) == 2, [m.format() for m in only]
        checks += 2

        # Player 2's clock 2 s ahead: with the offset its join line comes before the host's first lockstep line still,
        # and its state line moves before the host's.
        _, shifted = coop_log.merge([host, client], offsets={2: -2.0})
        order = [(m.player, m.category) for m in shifted]
        assert order.index((2, "state")) < order.index((1, "state")), order
        assert "14:03:23.000 p2" in shifted[1].format({2: -2.0}), shifted[1].format({2: -2.0})
        checks += 2

        found = coop_log.log_files(host)
        assert set(found[STAMP]) == {1, 2}, found
        checks += 1
    print(f"coop_log_test: ok, {checks} checks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
