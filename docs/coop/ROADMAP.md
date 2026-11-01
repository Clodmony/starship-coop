# Roadmap

Each work package (WP) becomes one GitHub issue with the `type/work-package` label, and one branch and PR when
someone takes it. The issue is the source of truth for who holds it and where it stands; this file only lists what
exists and what depends on what. Update this file when a WP is added, split or finished.

Work packages inside a milestone can run in parallel unless "needs" says otherwise.

## M0: Project and research

| WP | Title | Area | Needs | Done when |
|---|---|---|---|---|
| WP-01 | Build on Linux, Windows and macOS from this fork; note gotchas in BUILDING notes | build | | Each OS builds and boots to the title screen; steps and versions in NOTES.md |
| WP-02 | Determinism spike: same seeds + same inputs = same state? | sync, research | WP-01, WP-03 | Per-frame state hash of two runs on one machine compared over a full level; then across two machines; result in NOTES.md |
| WP-03 | Input recording and replay (record controller input per game frame, play it back) | test | WP-01 | A recorded Corneria run replays to the same level-complete screen |
| WP-04 | State hash: one hash per game frame over players, object arrays, RNG seeds and key globals | test, sync | WP-01 | Hash is logged per frame; a deliberate one-byte change shows up |
| WP-05 | Single-player assumptions audit: every use of `gPlayer[0]`, `gPlayer->`, `gMainController`, `gCamCount` in campaign code | engine, research | | List with file:line and a verdict per site in NOTES.md |
| WP-06 | On-rails mechanics: how player position, path progress and camera interact | engine, research | | Findings in NOTES.md with sources |
| WP-07 | Decide the game rules in DESIGN.md section 4 | design | WP-05 | Decision records merged |

## M1: Local co-op (two players, one PC, split screen)

| WP | Title | Area | Needs | Done when |
|---|---|---|---|---|
| WP-10 | Separate player count from camera count | engine | WP-05 | A `gCoopPlayerCount` (name to be agreed) drives player loops; single player unchanged (replay test) |
| WP-11 | Spawn and update player 2 in all-range levels | engine | WP-10 | Player 2 flies in Corneria's all-range section, scripted |
| WP-12 | Player 2 in on-rails levels | engine | WP-06, WP-10 | Player 2 flies the Corneria rail section without breaking path progress |
| WP-13 | Split-screen cameras and viewports for 2 players in campaign | render | WP-10 | Two viewports, each following its own player |
| WP-14 | Input routing: controller N to player N | input | WP-10 | Each player responds only to its own controller |
| WP-15 | HUD per player: shields, bombs, boost, lives | ui | WP-13 | Both HUDs drawn in their viewports |
| WP-16 | Game rules from WP-07: lives, down and respawn, route, score | engine | WP-07, WP-11 | Rules covered by replay tests |
| WP-17 | Wingman replacement (if decided): player 2 as Falco | engine | WP-07, WP-11 | Radio and team shields consistent |
| WP-18 | Vehicles: Landmaster and Blue Marine levels with 2 players | engine | WP-11 | Titania and Aquas playable by script |
| WP-19 | Bosses, cutscenes, level intro and level complete with 2 players | engine | WP-11 | Corneria start to finish, both players, scripted |

## M2: Online co-op

| WP | Title | Area | Needs | Done when |
|---|---|---|---|---|
| WP-20 | Transport interface + SDL2_net implementation behind `USE_NETWORKING` | net | WP-01 | Two processes exchange framed messages; loss and lag tested |
| WP-21 | Session: host, join, handshake (build id, ROM version, sim CVars) | net | WP-20 | Mismatches are refused with a clear message |
| WP-22 | Sync model implementation per decision 0002 | sync | WP-02, M1 core (WP-10..14) | Two clients finish Corneria with matching state hashes |
| WP-23 | Desync detection and report: compare hashes, dump the first diverging frame | sync, test | WP-04, WP-22 | A forced desync is caught and reported with the frame number |
| WP-24 | Join flow: lobby to level start for all players together | net, ui | WP-21 | Both players start the same level with the same seed |
| WP-25 | ImGui co-op menu: host, join, status, ping | ui | WP-21 | Usable without editing config files |
| WP-26 | Steam relay transport (Spacewar app 480), no port forwarding | net | WP-20 | Two PCs connect through Steam |
| WP-27 | Lag and loss testing with a chaos relay | test | WP-22 | Results with numbers in QA notes |

## M3: Release

| WP | Title | Area | Needs | Done when |
|---|---|---|---|---|
| WP-30 | CI artifacts named for co-op builds; version shown in game | build | | Builds downloadable from Actions |
| WP-31 | Player docs: install, host, join, known issues | docs | M2 | README "How to play" section written from a real run |
| WP-32 | Full campaign co-op playthrough (scripted where possible) | test | M2 | QA level `automated` or `verified-playthrough` in QA.md |
| WP-33 | Co-op Anything write-up and directory entry | docs | | PR to the hub, see HUB.md |

## Later

- Up to four players (versus mode already handles four `Player` structs).
- Rollback instead of plain lockstep, if decision 0002 picks lockstep and input delay hurts.
- Cross-platform play (Windows with Linux), if determinism holds across compilers.
- Late join and reconnect.
