# Starship Co-op

**Star Fox 64 campaign co-op for [Starship](https://github.com/HarbourMasters/Starship), the native PC port of the
Star Fox 64 decompilation.** Fly the Lylat System online with friends, each player in their own Arwing on their own
PC.

> **Status: pre-alpha.** There is no playable build yet. This repository is the shared workspace where the co-op
> layer is being built. See the [roadmap](docs/coop/ROADMAP.md) for what is planned and what is being worked on.

Starship Co-op is a fork of Starship. It contains no copyrighted assets: like Starship, it builds its game data from
your own US 1.0 or US 1.1 Star Fox 64 ROM on first launch.

## Current state

Last updated 2026-11-08. For the live list see the [open issues](https://github.com/Clodmony/starship-coop/issues) and
the [roadmap](docs/coop/ROADMAP.md).

**Two real games now stay in step through all of Corneria, on one PC.** Scripts fly both, over the real session, and
their state matches on every frame. On two PCs (Windows against Linux, played by hand since 2026-11-08) the games
still part in Corneria; each cause found becomes an issue. The first goal is two players on two PCs flying all of
Corneria with matching state.

### What works

Everything here is tested by scripts, on Corneria, on Linux; determinism also on Windows (Release and Debug). Two PCs (Windows host, Linux client) have played Corneria by hand since 2026-11-08; those runs still end in a desync, see below.

| Area | State |
|---|---|
| Players | Up to 4 Arwings fly Corneria on rails and in all-range, started by a debug setting. Titania and Macbeth with a Landmaster per player, Aquas with a Blue Marine per player; all three run by test pilots to level complete, bosses included (one PC) |
| Input | Each player has their own input source: a controller, a replay or the network |
| View | Camera, HUD and sound follow your own player, and the other player is an Arwing with a shield bar |
| Network | UDP transport with its own reliability, session handshake that refuses a different build, ROM or game setting and warns about a different platform, character choice |
| Menu | Co-op menu with host, join, characters, status and ping, and starting a level for everyone together |
| Steam relay | Built, off by default, tested on a fake Steam only |
| Determinism | Two runs of one build match on every frame through all of Corneria, and Release, Debug and frame rates from 30 to 360 match too. Two clients that draw different cameras, booted at different times and render different audio sample counts match on 15 Arwing levels (the first 80 seconds of each) |
| Lockstep | [Decision 0002](docs/coop/decisions/0002-sync-model.md) is built: inputs go over the network, every frame waits for every player, any player can pause, and a lost player ends the session cleanly |
| Desync check | Every client hashes its state on every frame and the clients compare. A desync names its frame, level, players and regions in the game, each game writes a report into its own folder, and the session ends. A tool compares two reports and names the first value that differs |
| Co-op log | Every game writes a log of the session into its own folder (session, transport, lockstep, desync check, game state, a state line per player each second, the game's own warnings) and streams it to the other players, so every folder holds every player's log, with the home folder and user name taken out. `python tools/coop/coop_log.py merge` makes one timeline of them |
| Tests | One command runs everything (`python tools/coop/test_all.py`): replays, a state hash per frame, deliberate-break tests, and uncapped, silent test games that can skip the drawing and start straight in a level. See [tools/coop](tools/coop/README.md) |

### In progress

The first Windows against Linux session parted in Corneria within 22 frames: the two C libraries round one `sinf` input differently ([#186](https://github.com/Clodmony/starship-coop/issues/186), [decision 0011](docs/coop/decisions/0011-deterministic-sine-in-sessions.md)). The fix is in progress; the next step is the same test again: see [TWO_PC_TEST.md](docs/coop/TWO_PC_TEST.md).

### What is open

| What | Where |
|---|---|
| What a session does after a desync: it ends now, the smallest honest choice, for the owner to revise | [WP-23](https://github.com/Clodmony/starship-coop/issues/21), [decision 0002](docs/coop/decisions/0002-sync-model.md) |
| A run on two PCs | [#2](https://github.com/Clodmony/starship-coop/issues/2): needed before a first public release. Windows Release matches itself, Debug and every rate ([#145](https://github.com/Clodmony/starship-coop/issues/145)); Windows against Linux parted at frame 22 on the first try ([#186](https://github.com/Clodmony/starship-coop/issues/186), [#187](https://github.com/Clodmony/starship-coop/issues/187)) |
| Real Steam test | [WP-26](https://github.com/Clodmony/starship-coop/issues/24): needs two PCs and two Steam accounts |
| Lag and loss testing | [WP-27](https://github.com/Clodmony/starship-coop/issues/25) |
| Game rules: lives, respawn, routes, score | Decisions [0006](docs/coop/decisions/0006-human-flown-wingman.md), [0007](docs/coop/decisions/0007-lives-and-respawn.md) and [0008](docs/coop/decisions/0008-routes-and-score.md) are Accepted, [0009](docs/coop/decisions/0009-vehicles-cutscenes-save.md) is Proposed (item 6, vehicles, is Accepted), then [WP-16](https://github.com/Clodmony/starship-coop/issues/14) and [WP-17](https://github.com/Clodmony/starship-coop/issues/15) |
| Other levels, bosses and cutscenes | [WP-19](https://github.com/Clodmony/starship-coop/issues/17), [#136](https://github.com/Clodmony/starship-coop/issues/136) |
| macOS build | [WP-01](https://github.com/Clodmony/starship-coop/issues/1) |
| Local split screen | [WP-13](https://github.com/Clodmony/starship-coop/issues/11), [WP-15](https://github.com/Clodmony/starship-coop/issues/13) |
| Player docs and a full campaign run | [WP-31](https://github.com/Clodmony/starship-coop/issues/27), [WP-32](https://github.com/Clodmony/starship-coop/issues/28) |

## Goal

| | Planned |
|---|---|
| Players | 2 (later up to 4, the number versus mode already supports) |
| Modes | Online: each player on their own PC. Local split screen comes later |
| What is shared | The whole level: enemies, bosses, items, wingmen, route choices, the story |
| Save | The host's save file; clients do not touch their own |

This is the plan, not the current state. The open design questions are in [docs/coop/DESIGN.md](docs/coop/DESIGN.md). The sync model is
deterministic lockstep, see [decision 0002](docs/coop/decisions/0002-sync-model.md).

## Building

Same as upstream Starship, see [docs/BUILDING.md](docs/BUILDING.md). Clone with submodules:

```sh
git clone --recursive https://github.com/Clodmony/starship-coop.git
# or, after a plain clone:
git submodule update --init --recursive
```

The co-op code builds as part of the normal Starship build; there is no separate project.

## Contributing

Many people (and their coding agents) work on this at the same time, so the workflow matters:

1. Read [CONTRIBUTING.md](CONTRIBUTING.md). Agents start at [AGENTS.md](AGENTS.md).
2. Pick an open work package from the [roadmap](docs/coop/ROADMAP.md) or the issue tracker and claim it on its issue.
3. One issue per branch (`issue/<issue>-<slug>`, from `coop`), one branch per pull request, draft first. `coop` is
   the default branch with all co-op work; `main` mirrors upstream Starship unchanged.

Design documents live in [docs/coop/](docs/coop/):

| Document | What it holds |
|---|---|
| [DESIGN.md](docs/coop/DESIGN.md) | Architecture, what the decomp tells us, open questions, where code goes |
| [ROADMAP.md](docs/coop/ROADMAP.md) | Milestones and work packages (one GitHub issue each) |
| [PROTOCOL.md](docs/coop/PROTOCOL.md) | Wire protocol rules and the message-id registry |
| [QA.md](docs/coop/QA.md) | QA levels, test harness plan, how to report results |
| [NOTES.md](docs/coop/NOTES.md) | Shared lab notebook: findings with sources |
| [decisions/](docs/coop/decisions/) | Architecture decision records |
| [MAINTAINING.md](docs/coop/MAINTAINING.md) | Upstream sync, GitHub settings, releases |

## Credits

- [Starship](https://github.com/HarbourMasters/Starship) by the HarbourMasters team (lead developers SonicDcer and
  Lywx) and its contributors: the PC port this project is built on. The upstream README is at
  [HarbourMasters/Starship](https://github.com/HarbourMasters/Starship#readme).
- The [Star Fox 64 decompilation](https://github.com/sonicdcer/sf64) and its contributors.
- [libultraship](https://github.com/Kenix3/libultraship) and [Torch](https://github.com/HarbourMasters/Torch).
- [Co-op Anything](https://github.com/cranebeaker/coop-anything): the playbook and agent workflow this project
  follows (MIT, see [LICENSES/coop-anything-MIT.txt](LICENSES/coop-anything-MIT.txt)).

## License

Code in this repository is released under CC0 1.0, the same as upstream Starship ([LICENSE.md](LICENSE.md)), so co-op
fixes can flow back upstream. Star Fox 64 is a trademark of Nintendo; this project is not affiliated with Nintendo.
Please don't ask for or share ROMs.
