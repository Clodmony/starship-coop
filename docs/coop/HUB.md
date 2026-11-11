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

### Why the entry says what it says

| Field | Value | Source |
|---|---|---|
| `coop_scope` | `players_visible`, `enemies`, `world_state` yes; `story` partial; `items` per-player | `README.md` "What the game does in a session": lockstep shares the whole level, the host's input drives cutscenes and radio, an item works for the player who takes it, lives and gold rings are the team's |
| `players` | 2 played, up to 4 in the code, three clients in scripted tests | `README.md` "Goal"; the `three` scenario of `tools/coop/lockstep_test.py` |
| `networking` | `direct-ip`: a client connects to a host and port; the played sessions were over a VPN | `src/port/coop/net/transport.h`, decision 0004, `README.md` "Host and join" |
| `status`, `qa` | `alpha`, `automated` | `docs/coop/QA.md` "Current project level"; the 66-minute campaign run is named in the notes, and `verified-playthrough` is for the owner to claim |
| `open_source` | `yes`, CC0 1.0 | `LICENSE.md` |
| `made_with_coop_anything` | `true` | `AGENTS.md` |

Updated 2026-11-11 after the merge of the combined test build (PR #263). The hub was searched again that day: no
`games/star-fox-64`, no directory entry (not even a "gap" row), and no issue or pull request that mentions Star Fox,
Starship or SF64. The entry passed the hub's validator (`validate_entry`).
