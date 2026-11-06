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
| `coop_scope` | `no` for all five: nothing is shared between clients yet | `docs/coop/QA.md`, "Current project level"; no call into `src/port/coop/session/` or `net/` from outside `src/port/coop/` |
| `players` | 2 planned, later up to 4 | `README.md`, "Goal" |
| `networking` | `direct-ip`: a client connects to a host and port. Loopback only so far | `src/port/coop/net/transport.h:92`, decision 0004 |
| `status`, `qa` | `prototype`, `none` | `README.md`, `docs/coop/QA.md` |
| `open_source` | `yes`, CC0 1.0 | `LICENSE.md` |
| `made_with_coop_anything` | `true` | `AGENTS.md` |
