# Co-op Anything hub

This project follows [Co-op Anything](https://github.com/cranebeaker/coop-anything). The hub holds the shared playbook
and a directory of co-op projects; each game's code lives in its own repository, like this one. Give back to it when
a phase finishes (hub AGENTS.md, "Sending what you learned back").

## Not done yet

Both steps go out under the owner's account, so only the owner sends them.

1. Open a "claim a game" issue on the hub for Star Fox 64 so others can find this project.
2. Send the directory entry and the write-up in a draft PR from a fork of the hub, branch `game/star-fox-64`.

## Drafts

| File here | Where it goes in the hub |
|---|---|
| [hub/star-fox-64.md](hub/star-fox-64.md) | `games/star-fox-64/README.md`, in the form of the hub's `templates/game.md` |
| [hub/directory-entry.yaml](hub/directory-entry.yaml) | Appended to `data/directory.yaml`, then run `python tools/build_directory.py` |

Before sending:

- Search the hub again for Star Fox 64: issues, PRs and `data/directory.yaml`.
- Check the status, the QA level and the results in the write-up against `docs/coop/QA.md`, and update `Last updated`,
  `last_activity` and `checked`. Keep the scope honest.
- The file references in the write-up are to one commit, named there. Recheck them if you move that commit.
- If the owner claims `verified-playthrough`, change it in three places together: `qa` in the entry, the QA line and
  section of the write-up, and "Current project level" in `docs/coop/QA.md`.

### Why the entry says what it says

| Field | Value | Source |
|---|---|---|
| `coop_scope` | `players_visible`, `enemies`, `world_state` yes; `story` partial; `items` per-player | `README.md` "What the game does in a session": lockstep runs one world on every PC, so the level, enemies and bosses are shared; an item works for the player who takes it, lives and gold rings are the team's. `story` is partial because the host alone drives cutscenes, the radio and the route, and a story scene of a wingman a human flies goes to another character (decision 0012) |
| `players` | 2 played, up to 4 in the code, 3 in scripted network tests | `README.md` "Goal" and "What works"; the `three` scenario of `tools/coop/lockstep_test.py` |
| `networking` | `direct-ip`: a client connects to a host and port; the played sessions were over a VPN, which is one way to reach the port, not a requirement | `src/port/coop/net/transport.h`, decision 0004, `README.md` "Host and join" |
| `status` | `alpha`: the nearest hub value. The README calls the project pre-alpha (no release, no download), and the notes say so | `README.md` status line; the hand-played campaign, issue #2 |
| `qa` | `automated`, the project's own level | `docs/coop/QA.md` "Current project level". The campaign finished by hand on 2026-11-09 and 2026-11-10 (issue #2) fits the hub's words for `verified-playthrough`, but it ran on a test build from before the merge, so the claim is left to the owner |
| `open_source` | `yes`, CC0 1.0 | `LICENSE.md` |
| `made_with_coop_anything` | `true` | `AGENTS.md` |

Checked on 2026-11-11, after the merge of the combined test build (PR #263, `coop` `a4867d99`). The hub was searched again
that day: no `games/star-fox-64`, no directory entry (not even a "gap" row), and no issue or pull request that mentions
Star Fox, Starship or SF64. Every figure in the write-up comes from `README.md`, `docs/coop/QA.md`, `docs/coop/NOTES.md`,
the decision records, the issues it links, or the merge gate of 2026-11-11 (`test_all.py`, 52 of 52 steps, plus the
`lockstep_test.py` scenarios `eels` and `map_saves`). The entry passed the hub's validator (`validate_entry`, `valid: true`).
