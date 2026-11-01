# QA

Every PR states the QA level it reached and how. Levels follow Co-op Anything:

| Level | Meaning |
|---|---|
| `none` | Compiles, or nothing was run. Say which. |
| `community-reports` | People played it and reported back, without a test plan. |
| `automated` | A scripted harness drove real game clients and checked shared state. Paste the summary. |
| `verified-playthrough` | A full campaign was finished in co-op, written up with known issues. |

Current project level: `none` (no co-op code yet).

## Test identity

Record with every result: commit hash, ROM version (US 1.0 or 1.1), OS, compiler and version, graphics backend,
number of clients, transport.

## Test plan (to be built in M0 and M2)

1. **Replays (WP-03).** Controller input recorded per game frame and played back. Single-player replays guard the rule
   that co-op off changes nothing: a replay recorded on upstream must give the same per-frame hashes on this fork.
2. **State hashes (WP-04).** One hash per game frame over players, object arrays, RNG seeds and key globals. Two
   clients compare hashes; the first different frame is the desync.
3. **Two clients on one machine.** Two Starship processes with their own config, save and log folders (never the
   owner's), connected over loopback, both fed replays.
4. **Network conditions (WP-27).** Lag, loss and reordering between the clients with a chaos relay, as in the hub's
   [network chaos testing](https://github.com/cranebeaker/coop-anything/blob/main/playbook/network-chaos-testing.md).
5. **Regression per bug.** Every desync fix comes with the replay that triggered it.

Running tests must never use or modify the owner's own `starship.cfg.json`, saves, `mods/` folder or running game.

## Results

Newest first. One block per run that matters (a milestone, a release, a QA level change).

```
date:       YYYY-MM-DD
commit:     <hash>
identity:   <ROM version, OS, compiler, backend>
what ran:   <command>
result:     <pass/fail counts, first failing frame>
```
