# Starship Co-op

**Star Fox 64 campaign co-op for [Starship](https://github.com/HarbourMasters/Starship), the native PC port of the
Star Fox 64 decompilation.** Fly the Lylat System online with friends, each player in their own Arwing on their own
PC.

> **Status: pre-alpha.** There is no playable build yet. This repository is the shared workspace where the co-op
> layer is being built. See the [roadmap](docs/coop/ROADMAP.md) for what is planned and what is being worked on.

Starship Co-op is a fork of Starship. It contains no copyrighted assets: like Starship, it builds its game data from
your own US 1.0 or US 1.1 Star Fox 64 ROM on first launch.

## Goal

| | Planned |
|---|---|
| Players | 2 (later up to 4, the number versus mode already supports) |
| Modes | Online: each player on their own PC. Local split screen comes later |
| What is shared | The whole level: enemies, bosses, items, wingmen, route choices, the story |
| Save | The host's save file; clients do not touch their own |

None of this works yet. The open design questions are in [docs/coop/DESIGN.md](docs/coop/DESIGN.md), and the choice
of sync model is still open in [decision 0002](docs/coop/decisions/0002-sync-model.md).

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
