# Starship Co-op

**Star Fox 64 campaign co-op for [Starship](https://github.com/HarbourMasters/Starship), the native PC port of the
Star Fox 64 decompilation.** Fly the Lylat System online with friends, each player in their own Arwing on their own
PC.

> **Status: pre-alpha.** There is no downloadable build yet: two people have played the whole campaign on two PCs
> with a build made from source, see [How to play](#how-to-play). This repository is the shared workspace where the
> co-op layer is being built. See the [roadmap](docs/coop/ROADMAP.md) for what is planned and what is being worked on.

Starship Co-op is a fork of Starship. It contains no copyrighted assets: like Starship, it builds its game data from
your own US 1.0 or US 1.1 Star Fox 64 ROM on first launch.

## How to play

For two players, each on their own PC, each with their own Star Fox 64 ROM. This section describes `coop` as of the merge of
2026-11-11, which holds what used to be the test build, because the campaign was played on it. Where
this section says "observed", a person played it; "from the code" and "from the record" are not playtests.

### What you need

- **The game's ROM, US 1.0 or 1.1, for each player.** Both players need the same version. It is never in this
  repository and must stay out of it. Starship builds its game data from your ROM; a source build needs it as
  `baserom.z64` in the repository folder, in z64 byte order (some dumps come in v64 or n64 order and must be swapped
  first; the ROM hashes it accepts are the entries of [`config.yml`](config.yml), and [CONTRIBUTING.md](CONTRIBUTING.md) has the build steps).
- **The same build on both PCs.** There is **no download yet**: the repository has no release
  ([Releases](https://github.com/Clodmony/starship-coop/releases) is empty) and GitHub CI has been off since 2026-11-07,
  so no current build is published. The workflow `Co-op build` (`.github/workflows/coop-build.yml`) can make a Windows
  and a Linux package, kept 14 days, when someone with write access re-enables and runs it. Otherwise each player
  builds the same commit from source: `python tools/coop/build.py --config Release --assets`, steps and gotchas in
  [docs/BUILDING.md](docs/BUILDING.md). The package holds the game and its settings, never game data.
- **Linux package:** it needs the shared libraries of Ubuntu 24.04 (`libzip.so.4`, `libtinyxml2.so.10`,
  `libspdlog.so.1.12`, `libfmt.so.9`) and does not start on a newer distribution
  ([#121](https://github.com/Clodmony/starship-coop/issues/121)). A source build has no such limit.
- **A way through the network.** Direct: the host's UDP port 46464 (the menu's default) must be reachable, which means
  the same home network, a port forwarded on the host's router, or both players on one virtual private network
  (observed: the campaign run used a direct connection). Allow the game through a firewall when it asks. The Steam
  relay is **not available**: it is built, off by default and tested on a fake Steam only
  ([#24](https://github.com/Clodmony/starship-coop/issues/24)).
- **Equal game settings.** Settings that change the game, such as cheats, must match.

### Both players run the same build

Press **F1** to open the menu bar. Its right end shows `Co-op <build id>`: the first 8 characters of the build id, and
`-dirty` after them for a build made from uncommitted changes (in a bar with no room for all of it, as in the default
window, only `<8 characters>-dirty`). Both players must see the same text, without `-dirty`: build from a clean
checkout of the same commit. The handshake compares the whole id, not only what the label shows. A joining player
whose build differs is **refused**: the join fails and the Co-op window prints
`Different game builds: host <id>, joining player <id>. Both need the same Starship Co-op build.` (`src/port/coop/session/identity.c`).
A different ROM version or a different game setting is refused the same way and names what differs. A different
platform (Windows against Linux) is **not** refused: both players see a warning that names the two platforms, and the
desync check below decides ([decision 0010](docs/coop/decisions/0010-cross-platform-sessions.md)).

### Host and join

1. Both players start the game, press F1, and open **Co-op** in the menu bar.
2. **Host:** leave **Port** at 46464 and click **Host a game**. The status line says `Hosting on port 46464, 1 of 2
   players.` Tell the other player your address (`<host address>`: your local IP on a home network, your public IP
   across the internet, or your VPN address).
3. **Joining player:** type `<host address>` into **Host's address**, the port the host shows into **Port**, and click
   **Join a game**. The status line shows the session and a ping.
4. **Characters** (from the record, [decision 0005](docs/coop/decisions/0005-players-and-characters.md)): the host
   always flies Fox; each other player picks Falco, Peppy or Slippy under **Your character**, and a wingman already
   taken cannot be chosen. A wingman nobody chose stays a computer wingman.
5. **Host only:** **Start at** picks the level to begin with (Corneria unless you change it). A level
   started this way carries none of the route a campaign builds on the way. Then **Start the level**, once every player
   has chosen. Both games go to the level together.
6. Fly. **Either player's Start pauses both games**; the pause menu's cursor, retry and quit belong to the host.
   **Leave** ends a session for the one who leaves, and for both in a session of two. If the connection drops or a
   player sends no input for 10 seconds, the host waits 15 seconds for that player to come back, which its game does
   by itself; then, as after a desync, the level starts again for everyone who is there, at the last checkpoint or
   from its start.

### What the game does in a session

| What | Rule | Where it stands |
|---|---|---|
| Lives | One shared pool of lives for the team ([decision 0007](docs/coop/decisions/0007-lives-and-respawn.md)). | The pool is the game's own life count; any life anyone collects goes into it. It starts at 2 plus 1 for each player after the first: 3 for two players, 4 for three, 5 for four, read in scripted sessions of two, three and four clients on one Linux PC ([#14](https://github.com/Clodmony/starship-coop/issues/14)); not yet played by hand. A player who joins or leaves between two levels does not change it. Every client's HUD shows the pool, also one that looks through player 2 to 4 ([#335](https://github.com/Clodmony/starship-coop/issues/335)). |
| A second player goes down | They leave the level, and 150 game frames (about 5 seconds) later come back beside Fox, at full shields, for one life from the pool. At pool 0 they wait for a life or the next level. | Observed in scripted two-client runs on Corneria ([#14](https://github.com/Clodmony/starship-coop/issues/14), NOTES.md, 2026-11-09); nobody has reported it from a hand-played run. |
| Fox (the host) goes down | Fox is refilled in place for one life from the pool (rule 12), on 16 levels; with no life left the original rule holds, game over. The owner opened all 16 levels on 2026-11-11 instead of one at a time. | Scripted two-client runs on every one of the 16 levels (NOTES.md, 2026-11-11, "Fox goes down"); nobody has seen it in play. |
| Shields between levels | Every player's shields carry over and heal by 64 at each level load. | wingman part, [#15](https://github.com/Clodmony/starship-coop/issues/15); scripted, not hand-checked. |
| Who steers (the path owner) | The host: at a path split the side comes from the host's position, and on the map only the host's input decides go on, change path and retry ([decision 0008](docs/coop/decisions/0008-routes-and-score.md)). Any player can earn a route or a warp. Boost and brake of **any** player change the level's speed. | Boost: [#194](https://github.com/Clodmony/starship-coop/issues/194). The rest is the original game's path handling plus the record. |
| Score | Hits by any player count for the team; medals keep the original check, so a player who is down at the clear costs a medal. | From the record; not checked in play. |
| The level intro | If the host's save has the level cleared, either player's Start skips it on both games; otherwise Start does nothing there. The host's save decides, not the joining player's. | [#191](https://github.com/Clodmony/starship-coop/issues/191), scripted; the first sentence rests on a scripted run. |
| Items | A laser, crate or wing repair works for the player who takes it. | [#201](https://github.com/Clodmony/starship-coop/issues/201), scripted. |
| Enemies' aim | Most aimed enemy shots go at the nearest human player, not always at Fox. | [#193](https://github.com/Clodmony/starship-coop/issues/193), scripted; shots that aim some other way, and bosses, are not covered (see the issues below). |
| Vehicles | Each player gets the level's Landmaster (Titania, Macbeth) or Blue Marine (Aquas). | [#16](https://github.com/Clodmony/starship-coop/issues/16). Observed in Macbeth with a second player in the campaign run. Titania and Aquas: scripted only. |
| Save | Every game plays on the host's progress, which the host sends once just before the level starts; each game keeps its own sound settings, and no game writes its real save during a session, the host's included. What a session earns is not kept ([decision 0009](docs/coop/decisions/0009-vehicles-cutscenes-save.md), item 8, Accepted). | **Observed**: a Windows host and a Linux joining player went through Corneria, two maps, Sector Y and Aquas with different saves on the evening of 2026-11-10; scripted too (`map_saves`). |
| Cutscenes and radio | Follow the host's timing; a player waits out a cutscene. A wingman a human flies has its radio lines kept and the calls for help dropped in all-range ([decision 0006](docs/coop/decisions/0006-human-flown-wingman.md)). | From the records. |

### If the games part

Every game hashes its state each frame and the games compare. On a difference the game stops the session, says so on
screen with the frame and what differs, and writes `coop-desync-<frame>-player<n>.txt` into its own folder, with the
co-op logs `coop-log-*`. What to send back: [docs/coop/TWO_PC_TEST.md](docs/coop/TWO_PC_TEST.md), "What to send back".
The reports and logs can hold your user name in file paths: do not post them in a public issue unchecked.

### What a real run showed (observed)

On 2026-11-09 and 2026-11-10 two people played the whole campaign on two PCs, a Windows host and a Linux joining
player, over a direct connection with a VPN ([#2](https://github.com/Clodmony/starship-coop/issues/2)): Corneria, Meteo,
Fortuna, Solar, Macbeth, Area 6, Venom 2, Venom with Andross, Venom 2 again, then the ending, 66 minutes by the log.
The test build of that day had 0 desync lines, 0 errors and 0 warnings; ping median 33 ms, 95th percentile 52 ms,
worst 90 ms; the games waited for the other player 31 times, 8 seconds in all. Two earlier builds had parted on the way
(fixed since). That run covered a build of 2026-11-09 and not the newer test build.

### Known issues

**Seen in play (observed)**

- The last two bosses, the Gorgon in Area 6 and Andross, **ignore the second player** ([#213](https://github.com/Clodmony/starship-coop/issues/213); a fix is in progress, not in any build yet).

**From the open issues (from the code or the tracker, not re-played)**

- Some items and triggers still look at player 0 only: the Meteo warp ring, the gold ring count, kill credit and the
  wing-broken trigger ([#210](https://github.com/Clodmony/starship-coop/issues/210)). The second player's Solar heat
  drain is fixed in the test build ([#200](https://github.com/Clodmony/starship-coop/issues/200)).
- A scene that would show a wingman a human flies, such as Corneria's rescue call to Slippy and then Peppy, is
  decided in [decision 0012](docs/coop/decisions/0012-taken-wingman-scenes.md) and built in two steps; the second step
  (Katt and Bill) is open ([#202](https://github.com/Clodmony/starship-coop/issues/202)).
- Scripted radio lines are lost while player index 0 is not active ([#88](https://github.com/Clodmony/starship-coop/issues/88)).
- The heat alarm, the pause reticle and billboards still follow player 0 on every client
  ([#140](https://github.com/Clodmony/starship-coop/issues/140)), and what a client sees while their own ship is out
  is player 0's view ([#30](https://github.com/Clodmony/starship-coop/issues/30)).
- Windows and Linux: nobody has shown that two builds compute the same game beyond the one campaign run above. Titania
  has 18 statements of the kind that parted the two in Macbeth
  ([#209](https://github.com/Clodmony/starship-coop/issues/209)); a Titania session between them can part.
- **Not covered by any run:** Sector X, Sector Y, Sector Z, Katina, Bolse, Zoness, Titania and Aquas with two real
  players; a Release build in a campaign; more than two players; the Steam transport; lag and loss
  ([#25](https://github.com/Clodmony/starship-coop/issues/25)). Start-at-level sessions beyond Corneria are only
  scripted for their first 40 seconds ([#129](https://github.com/Clodmony/starship-coop/issues/129)).
- The map after a level started with **Start at** is untested.
- Local split screen does not exist yet.

No dates are promised for any of this.

## Current state

Last updated 2026-11-12. For the live list see the [open issues](https://github.com/Clodmony/starship-coop/issues) and
the [roadmap](docs/coop/ROADMAP.md).

**Two real games stay in step through all of Corneria on one PC, and two people have played the whole campaign on two
PCs.** On 2026-11-09 and 2026-11-10 a Windows host and a Linux joining player flew from Corneria to the ending with no
desync, on a test build that is `coop` plus the open pull requests below (observed, [#2](https://github.com/Clodmony/starship-coop/issues/2)).
That build is merged into `coop` (2026-11-11), which fixes the first Windows against Linux fault
([#186](https://github.com/Clodmony/starship-coop/issues/186)); the stacked pull requests it was made of are closed with a note.
Sessions on the evening of 2026-11-10 (a Windows host and a Linux joining player, up to three levels and two maps) found
and fixed two more faults: the map reading each PC's own save, and an Aquas eel reading its smoke position from outside an array. The first public release still waits for a Windows Release check
([#145](https://github.com/Clodmony/starship-coop/issues/145)) and for the open issues below.

**Added on 2026-11-11, tested by scripts on one Linux PC and not yet played by hand:** a lost player or a desync starts
the level again from the host's state instead of ending the session ([decision 0014](docs/coop/decisions/0014-the-session-survives.md)),
Fox is refilled in place for a life ([decision 0007](docs/coop/decisions/0007-lives-and-respawn.md), rule 12, on 16
levels), a player's name shows over the other ships ([#257](https://github.com/Clodmony/starship-coop/issues/257),
[#258](https://github.com/Clodmony/starship-coop/issues/258)), and the test pilot finishes all 17 levels with two clients,
equal on every frame, while Sector X's Spyborg and Titania's Goras go after the nearest human
([#220](https://github.com/Clodmony/starship-coop/issues/220)). A real connection drop and Windows are not tried. **Added on 2026-11-14, tested by scripts on one Linux PC:** the test pilots fly the campaign with three and four real clients ([#322](https://github.com/Clodmony/starship-coop/issues/322)): all 18 entries of the route with four, five levels with three, each once, equal on every frame; which levels, how many frames and what was not looked at (the picture, single rules with a third or fourth player) is in [docs/coop/QA.md](docs/coop/QA.md). The rules for a hit and an aim that have a scenario of their own (enemies' shots, Macbeth's bombs, beams, turrets and car blast, Sector Y's robots, the Granga's plasma, the explosions of Andross' escape) were then run with three and four clients and held for the third and the fourth player ([#329](https://github.com/Clodmony/starship-coop/issues/329)); what those runs could not show is in the same file.

**Added on 2026-11-12, tested by scripts on one Linux PC and not yet played by hand:** Macbeth's train end goes after the nearest tank, and its bomb fire and beams hurt every tank, not player 0 only ([#220](https://github.com/Clodmony/starship-coop/issues/220), [#272](https://github.com/Clodmony/starship-coop/issues/272)); Sector Y's robots spread their shots around the human they aim at ([#273](https://github.com/Clodmony/starship-coop/issues/273)); the level clear screen waits for every human's A (30 s at most) ([#252](https://github.com/Clodmony/starship-coop/issues/252)); a restarted session is tested beyond Corneria (the map, the levels' own checkpoints, four players) ([#267](https://github.com/Clodmony/starship-coop/issues/267)); five stray reads and one write that a sanitizer sweep of every campaign level found are fixed ([#262](https://github.com/Clodmony/starship-coop/issues/262)). The test pilots now fly Andross' escape and the campaign runs Venom 2 through the brain and the escape to the ending, equal on every frame ([#275](https://github.com/Clodmony/starship-coop/issues/275)). **Added on 2026-11-13, tested by scripts on one Linux PC:** the explosions of Andross' escape hit every human, not player 0 only: a second player caught in one goes down and comes back beside player 0 for a life of the pool; the exit stays player 0's ([#282](https://github.com/Clodmony/starship-coop/issues/282)).

**Added on 2026-11-13, tested by scripts on one Linux PC and not yet played by hand:** in Macbeth a train car's turret shots and the blast of train car 7 hurt every tank, not player 0 only ([#280](https://github.com/Clodmony/starship-coop/issues/280)); Sector Y's robots turn head and gun to the human they shoot at, and the Granga's plasma spread stays around the human it aims at ([#281](https://github.com/Clodmony/starship-coop/issues/281)).

### What works

Everything here is tested by scripts, on Linux; determinism also on Windows (Release and Debug). Two PCs (Windows host, Linux client) have played by hand since 2026-11-08: the first runs parted in Corneria (fixed), the campaign run of 2026-11-09 did not part.

| Area | State |
|---|---|
| Players | Up to 4 players, started from the co-op menu; with 3 and 4 tested by scripts only (the campaign's levels once each with four test pilots, #322), not played by hand. Up to 4 Arwings fly Corneria on rails and in all-range. Titania and Macbeth with a Landmaster per player, Aquas with a Blue Marine per player; all three run by test pilots to level complete, bosses included (one PC) |
| Input | Each player has their own input source: a controller, a replay or the network |
| View | Camera, HUD and sound follow your own player, and the other player is an Arwing with a shield bar |
| Network | UDP transport with its own reliability, session handshake that refuses a different build, ROM or game setting and warns about a different platform, character choice |
| Menu | Co-op menu with host, join, characters, status and ping, and starting a level for everyone together |
| Steam relay | Built, off by default, tested on a fake Steam only |
| Determinism | Two runs of one build match on every frame through all of Corneria, and Release, Debug and frame rates from 30 to 360 match too. Two clients that draw different cameras, booted at different times and render different audio sample counts match on 15 Arwing levels (the first 80 seconds of each) |
| Lockstep | [Decision 0002](docs/coop/decisions/0002-sync-model.md) is built: inputs go over the network, every frame waits for every player, any player can pause, and a lost player or a desync starts the level again for the others ([decision 0014](docs/coop/decisions/0014-the-session-survives.md)); the session ends only when nobody is left or the games part twice in a row |
| Desync check | Every client hashes its state on every frame and the clients compare. A desync names its frame, level, players and regions in the game, each game writes a report into its own folder, and the level starts again from the host's state (a second desync right after ends the session). A tool compares two reports and names the first value that differs |
| Co-op log | Every game writes a log of the session into its own folder (session, transport, lockstep, desync check, game state, a state line per player each second, the game's own warnings) and streams it to the other players, so every folder holds every player's log, with the home folder and user name taken out. `python tools/coop/coop_log.py merge` makes one timeline of them |
| Tests | One command runs everything (`python tools/coop/test_all.py`): replays, a state hash per frame, deliberate-break tests, and uncapped, silent test games that can skip the drawing and start straight in a level. See [tools/coop](tools/coop/README.md) |

### In progress

Nothing waits in a pull request (the work of 2026-11-12 above is in `coop` with the combined test build 6, which nobody has played by hand). The next step is the same two-PC campaign again on the build that is on `coop` now: see [TWO_PC_TEST.md](docs/coop/TWO_PC_TEST.md). The player-facing text above was written for the test build and has had only the corrections this merge needs; a full pass is still open ([WP-31](https://github.com/Clodmony/starship-coop/issues/27)).

### What is open

| What | Where |
|---|---|
| What a session does after a desync or a lost player: the level starts again from the host's state, proven with real games on one PC only; the text of the rules waits for the owner | [decision 0014](docs/coop/decisions/0014-the-session-survives.md), [WP-23](https://github.com/Clodmony/starship-coop/issues/21) |
| A run on two PCs | [#2](https://github.com/Clodmony/starship-coop/issues/2): needed before a first public release. Windows Release matches itself, Debug and every rate ([#145](https://github.com/Clodmony/starship-coop/issues/145)); Windows against Linux parted at frame 22 on the first try ([#186](https://github.com/Clodmony/starship-coop/issues/186), [#187](https://github.com/Clodmony/starship-coop/issues/187)) |
| Real Steam test | [WP-26](https://github.com/Clodmony/starship-coop/issues/24): needs two PCs and two Steam accounts |
| Lag and loss testing | [WP-27](https://github.com/Clodmony/starship-coop/issues/25) |
| Game rules: lives, respawn, routes, score | Decisions [0006](docs/coop/decisions/0006-human-flown-wingman.md), [0007](docs/coop/decisions/0007-lives-and-respawn.md) [0008](docs/coop/decisions/0008-routes-and-score.md) and [0012](docs/coop/decisions/0012-taken-wingman-scenes.md) are Accepted, [0009](docs/coop/decisions/0009-vehicles-cutscenes-save.md) is Proposed (item 6, vehicles, is Accepted), then [WP-16](https://github.com/Clodmony/starship-coop/issues/14) and [WP-17](https://github.com/Clodmony/starship-coop/issues/15) |
| Other levels, bosses and cutscenes | [WP-19](https://github.com/Clodmony/starship-coop/issues/17), [#136](https://github.com/Clodmony/starship-coop/issues/136) |
| macOS build | [WP-01](https://github.com/Clodmony/starship-coop/issues/1) |
| Local split screen | [WP-13](https://github.com/Clodmony/starship-coop/issues/11), [WP-15](https://github.com/Clodmony/starship-coop/issues/13) |
| A downloadable build, the full campaign run with scripts, and the rest of the player docs | [WP-31](https://github.com/Clodmony/starship-coop/issues/27) (the "How to play" section above is written; a download is missing), [WP-32](https://github.com/Clodmony/starship-coop/issues/28) |

## Goal

| | Planned |
|---|---|
| Players | 2 (later up to 4, the number versus mode already supports) |
| Modes | Online: each player on their own PC. Local split screen comes later |
| What is shared | The whole level: enemies, bosses, items, wingmen, route choices, the story |
| Save | The host's progress goes to every game once at the start; no game writes a real save in a session |

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
