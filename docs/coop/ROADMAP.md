# Roadmap

**Priority: online co-op.** Each player plays on their own PC. Local split screen is deferred until after the first
release (see "Later").

**Two players first.** M1 and M2 are for two players. [Decision 0005](decisions/0005-players-and-characters.md) allows
up to four, so nothing built for M1 and M2 may block that. Three and four players have no work package of their own:
the game layer takes up to four (the menu's "x of 4 players"), and what scripts have flown with three and four games
is listed in [QA.md](QA.md) (the record of #322, and the rows `rails-4`, `survive_three`, `survive_gaps_four`). What is
not in those lists is not tested with more than two.

Each work package (WP) is one GitHub issue with the `type/work-package` label, and one branch and PR when someone
takes it. The issue is the source of truth for who holds it and where it stands; this file only lists what exists and
what depends on what. Update this file when a WP is added, split, moved or finished. WP ids never change, even when a
WP moves to another milestone.

Work packages inside a milestone can run in parallel unless "needs" says otherwise.

## Critical path

WP-01 build → WP-03 replay + WP-04 state hash → **WP-02 determinism spike → decision 0002 (sync model, accepted:
lockstep)** → WP-22 sync → two players finish Corneria online. The engine work (WP-10, WP-11, WP-12, WP-28) and the network work (WP-20,
WP-21) run in parallel with the spike; neither waits for the other.

## M0: Project and research

| WP | Issue | Title | Area | Needs | Done when |
|---|---|---|---|---|---|
| WP-01 | [#1](https://github.com/Clodmony/starship-coop/issues/1) | Build on Linux, Windows and macOS from this fork; note gotchas in BUILDING notes | build | | Each OS builds and boots to the title screen; steps and versions in NOTES.md |
| WP-02 | [#2](https://github.com/Clodmony/starship-coop/issues/2) | Determinism spike: same seeds + same inputs = same state? | sync, research | WP-01, WP-03 | Per-frame state hash of two runs on one machine compared over a full level; then across two machines; result in NOTES.md |
| WP-03 | [#3](https://github.com/Clodmony/starship-coop/issues/3) | Input recording and replay (record controller input per game frame, play it back) | test | WP-01 | A recorded Corneria run replays to the same level-complete screen |
| WP-04 | [#4](https://github.com/Clodmony/starship-coop/issues/4) | State hash: one hash per game frame over players, object arrays, RNG seeds and key globals | test, sync | WP-01 | Hash is logged per frame; a deliberate one-byte change shows up |
| WP-05 | [#5](https://github.com/Clodmony/starship-coop/issues/5) | Single-player assumptions audit: every use of `gPlayer[0]`, `gPlayer->`, `gMainController`, `gCamCount` in campaign code | engine, research | | List with file:line and a verdict per site in NOTES.md |
| WP-06 | [#6](https://github.com/Clodmony/starship-coop/issues/6) | On-rails mechanics: how player position, path progress and camera interact | engine, research | | Findings in NOTES.md with sources |
| WP-07 | [#7](https://github.com/Clodmony/starship-coop/issues/7) | Decide the game rules in DESIGN.md section 4 | design | WP-05 | Decision records merged |

Research for later records under WP-07, opened from decision 0005:

- Wingman events and routes: [#59](https://github.com/Clodmony/starship-coop/issues/59)
- Wingman radio lines: [#60](https://github.com/Clodmony/starship-coop/issues/60)
- How a downed wingman returns: [#61](https://github.com/Clodmony/starship-coop/issues/61)

## M1: Online co-op, first level

Two players on two PCs fly Corneria together, start to finish, with matching state hashes.

| WP | Issue | Title | Area | Needs | Done when |
|---|---|---|---|---|---|
| WP-10 | [#8](https://github.com/Clodmony/starship-coop/issues/8) | Separate player count from camera count | engine | WP-05 | A `gCoopPlayerCount` (name to be agreed) drives player loops; single player unchanged (replay test) |
| WP-11 | [#9](https://github.com/Clodmony/starship-coop/issues/9) | Spawn and update player 2 in all-range levels | engine | WP-10 | A second player, driven by a replay file (later by the network), flies in Corneria's all-range section |
| WP-12 | [#10](https://github.com/Clodmony/starship-coop/issues/10) | Player 2 in on-rails levels | engine | WP-06, WP-10 | Player 2 flies the Corneria rail section without breaking path progress |
| WP-14 | [#12](https://github.com/Clodmony/starship-coop/issues/12) | Input source per player: local controller for your own player, network or replay for the others | input | WP-10 | The local player answers only to the local controller; other players follow their input stream |
| WP-28 | [#30](https://github.com/Clodmony/starship-coop/issues/30) | Local player view: camera, HUD and sound follow the client's own player, which is not always player 0 | render, ui | WP-10 | On each client the camera and HUD follow that client's player; the other player is drawn as an Arwing with a shield bar |
| WP-16 | [#14](https://github.com/Clodmony/starship-coop/issues/14) | Game rules from WP-07: lives, down and respawn, route, score | engine | WP-07, WP-11 | Rules covered by replay tests |
| WP-19 | [#17](https://github.com/Clodmony/starship-coop/issues/17) | Bosses, cutscenes, level intro and level complete with 2 players | engine | WP-11 | Corneria start to finish, both players, scripted |
| WP-20 | [#18](https://github.com/Clodmony/starship-coop/issues/18) | Transport interface + SDL2_net implementation behind `USE_NETWORKING` | net | WP-01 | Two processes exchange framed messages; loss and lag tested |
| WP-21 | [#19](https://github.com/Clodmony/starship-coop/issues/19) | Session: host, join, handshake (build id, ROM version, sim CVars) | net | WP-20 | Mismatches are refused with a clear message |
| WP-22 | [#20](https://github.com/Clodmony/starship-coop/issues/20) | Sync model implementation per decision 0002 | sync | WP-02 (its one-PC half), WP-10, WP-11, WP-12, WP-14 | Two clients finish Corneria with matching state hashes |
| WP-23 | [#21](https://github.com/Clodmony/starship-coop/issues/21) | Desync detection and report: compare hashes, dump the first diverging frame | sync, test | WP-04, WP-22 | A forced desync is caught and reported with the frame number |
| WP-24 | [#22](https://github.com/Clodmony/starship-coop/issues/22) | Join flow: lobby to level start for all players together | net, ui | WP-21, WP-34 | Both players start the same level with the same seed and their chosen characters; a character already taken is refused |
| WP-25 | [#23](https://github.com/Clodmony/starship-coop/issues/23) | ImGui co-op menu: host, join, status, ping | ui | WP-21, WP-34 | Usable without editing config files; a character already taken cannot be chosen |
| WP-34 | [#58](https://github.com/Clodmony/starship-coop/issues/58) | Carry each player's character through the session | net | WP-21 | All clients agree on every player's character; a character already taken is refused; session tests cover both |

## M2: Online co-op, whole campaign

| WP | Issue | Title | Area | Needs | Done when |
|---|---|---|---|---|---|
| WP-17 | [#15](https://github.com/Clodmony/starship-coop/issues/15) | Wingman replacement: players 2-4 replace the chosen wingman | engine | WP-07, WP-11, WP-34 | Radio and team shields consistent, tested with two players |
| WP-18 | [#16](https://github.com/Clodmony/starship-coop/issues/16) | Vehicles: Landmaster and Blue Marine levels with 2 players | engine | WP-11 | Titania and Aquas playable by script |
| WP-26 | [#24](https://github.com/Clodmony/starship-coop/issues/24) | Steam relay transport (Spacewar app 480), no port forwarding | net | WP-20 | Two PCs connect through Steam |
| WP-27 | [#25](https://github.com/Clodmony/starship-coop/issues/25) | Lag and loss testing with a chaos relay | test | WP-22 | Results with numbers in QA notes |

## M3: Release

[Decision 0002](decisions/0002-sync-model.md) names two conditions before a first public release: the two-machine run
([#2](https://github.com/Clodmony/starship-coop/issues/2)), and `hash_check.py builds` and `rates` on Windows Release
([#145](https://github.com/Clodmony/starship-coop/issues/145)).

| WP | Issue | Title | Area | Needs | Done when |
|---|---|---|---|---|---|
| WP-30 | [#26](https://github.com/Clodmony/starship-coop/issues/26) | CI artifacts named for co-op builds; version shown in game | build | | Builds downloadable from Actions |
| WP-31 | [#27](https://github.com/Clodmony/starship-coop/issues/27) | Player docs: install, host, join, known issues | docs | M2 | README "How to play" section written from a real run |
| WP-32 | [#28](https://github.com/Clodmony/starship-coop/issues/28) | Full campaign co-op playthrough (scripted where possible) | test | M2 | QA level `automated` or `verified-playthrough` in QA.md |
| WP-33 | [#29](https://github.com/Clodmony/starship-coop/issues/29) | Co-op Anything write-up and directory entry | docs | | PR to the hub, see HUB.md |

## Later

Deferred work packages keep their issues, in the GitHub milestone "Later".

| WP | Issue | Title | Area | Needs | Done when |
|---|---|---|---|---|---|
| WP-13 | [#11](https://github.com/Clodmony/starship-coop/issues/11) | Local split screen: cameras and viewports for 2 players on one PC | render | WP-10, WP-28 | Two viewports, each following its own player |
| WP-15 | [#13](https://github.com/Clodmony/starship-coop/issues/13) | Local split screen: HUD per viewport | ui | WP-13 | Both HUDs drawn in their viewports |

- Three and four players beyond what is tested. Decision 0005 sets the rules and the game layer seats up to four
  players; `campaign.py --players 3|4` flies a level with three or four real clients
  ([#322](https://github.com/Clodmony/starship-coop/issues/322)). Open: the levels not yet flown that way, the rules
  written for one extra player (which wingman a third or fourth player gets, their share of the lives), what the
  camera, the HUD and the name tags show with three other ships, and a session of three or four PCs. The list of what
  ran is in QA.md.
- Rollback instead of plain lockstep, if input delay hurts (decision 0002).
- Cross-platform play (Windows with Linux), if determinism holds across compilers. A platform difference warns and the
  session runs (decision 0010); the desync check shows whether the two stay in step.
- Late join and reconnect.
