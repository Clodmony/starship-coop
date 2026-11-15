# Starship Co-op vX.Y.Z-coop-alpha1

Gated commit: TODO the 40-digit hash of the commit the full gate ran on
Gate result: PASS TODO N of N steps, date, `python tools/coop/test_all.py --mixed ...` (PASS only; a release is never cut from a failed gate)

TODO One paragraph: what this release is, in plain words. A pre-release (a name with alpha, beta or rc) says so here and says which
of the two release conditions of decision 0002 is not done yet (a run on two real PCs, #2; Windows Release hash checks, #145).

## What you need

- Your own Star Fox 64 ROM, US 1.0 or 1.1. Both players need the same version. The download holds no ROM and no `sf64.o2r`; the game
  asks for the ROM on its first start and makes `sf64.o2r` from it.
- The same build on both PCs: the game shows its build id at the right end of the menu bar (F1), and a player whose build differs is refused.
- A way through the network: the host's UDP port 46464 must be reachable (the same network, a forwarded port, or a virtual private network).
- Windows 10 or later (x64), or Linux x64 with the libraries of Ubuntu 24.04. There is no macOS build.

Files: `starship-coop-<id>-windows-x64.zip`, `starship-coop-<id>-linux-x64.tar.gz`, `SHA256SUMS.txt`.

## How to play together

TODO Three or four lines: unpack, start, give the ROM, open the Co-op window, one player hosts, the other types the host's address and
joins. Link the README's "Host and join" section.

## QA

TODO The QA level (docs/coop/QA.md) and what the gate covered, with the gate's numbers (steps, scenarios, minutes) and the platforms the
tests ran on. Say plainly what has not been run: Windows, two real PCs, a person playing, whatever applies.

## Known issues

TODO Everything known that does not work or is not decided yet, each with its issue number, in the words a player understands.
Windows against Linux (decision 0010: a platform difference warns, the session runs): say what has been tried.

## Not in this release

- Local split screen, voice, the Steam transport (not tested on real Steam accounts), macOS.

---

Checksums: `sha256sum -c SHA256SUMS.txt` next to the downloaded files. Report problems as an issue on the repository, with the build id and
the host's and the joining player's logs.
