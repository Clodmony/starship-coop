# Roadmap

**Priority: online co-op.** Each player plays on their own PC. Local split screen is deferred until after the first
release (see "Later").

**Two players first.** M1 and M2 are for two players. [Decision 0005](decisions/0005-players-and-characters.md) allows
up to four, so nothing built for M1 and M2 may block that. Three and four players have no work package of their own:
the game layer takes up to four (the menu's "x of 4 players"), and what scripts have flown with three and four games
is listed in [QA.md](QA.md) (the records of #322, #329, #332 and #342, and the rows `rails-4`, `survive_three`,
`survive_gaps_four`, `aim_four`). What is not in those lists is not tested with more than two, and nothing is tested
with people at the controls beyond two.

Each work package (WP) is one GitHub issue with the `type/work-package` label, and one branch and PR when someone
takes it. The issue is the source of truth for who holds it and where it stands; this file only lists what exists and
what depends on what. Update this file when a WP is added, split, moved or finished. WP ids never change, even when a
WP moves to another milestone.

Work packages inside a milestone can run in parallel unless "needs" says otherwise.

## Where we stand (2026-11-15)

This is the state of the combined test build 6 (`issue/combined-test-6`, PR #285), which holds all the work since the last
merge into `coop`: `coop` is at `07c03c1f` (2026-11-11), the combined build is 229 commits ahead of it and waits for the
owner's word to merge. Sources: [QA.md](QA.md), the README's "Current state", the issues.

- **Built, tested by scripts on one Linux PC:** online lockstep co-op for up to four players through the whole campaign
  (the test pilots fly every level with two clients, and the levels with three and four, equal on every frame); the
  session rules of decisions 0005 to 0008, 0010 to 0013 and 0015; a lost player or a desync starts the level again from the
  host's state (decision 0014); the view for the client's own player; the co-op menu; the transport. The full run,
  `python tools/coop/test_all.py --mixed`, passed 59 of 59 steps in 20 minutes on 2026-11-14 (QA.md).
- **Played by hand:** a Windows host and a Linux joining player flew from Corneria to the ending without a desync on
  2026-11-09 and 2026-11-10, on a test build before the combined build. Nobody has played the combined build, and nobody
  has played with three or four people.
- **Not tried:** a real connection that drops between two PCs; Windows against Linux on the current build; Windows Release
  hash checks ([#145](https://github.com/Clodmony/starship-coop/issues/145)); the Steam transport on real Steam; macOS;
  anyone outside the owner and one collaborator.
- **Release:** a workflow that makes a draft GitHub Release from a tag is built and has never run
  ([#358](https://github.com/Clodmony/starship-coop/issues/358)); see "Before a first public release" below.

## Critical path

WP-01 build → WP-03 replay + WP-04 state hash → **WP-02 determinism spike → decision 0002 (sync model, accepted:
lockstep)** → WP-22 sync → two players finish Corneria online. The engine work (WP-10, WP-11, WP-12, WP-28) and the network work (WP-20,
WP-21) run in parallel with the spike; neither waits for the other.

## M0: Project and research

| WP | Issue | Title | Area | Needs | Done when | Status |
|---|---|---|---|---|---|---|
| WP-01 | [#1](https://github.com/Clodmony/starship-coop/issues/1) | Build on Linux, Windows and macOS from this fork; note gotchas in BUILDING notes | build | | Each OS builds and boots to the title screen; steps and versions in NOTES.md | Linux and Windows build done (`docs/BUILDING.md`). **Open:** macOS has not been built. |
| WP-02 | [#2](https://github.com/Clodmony/starship-coop/issues/2) | Determinism spike: same seeds + same inputs = same state? | sync, research | WP-01, WP-03 | Per-frame state hash of two runs on one machine compared over a full level; then across two machines; result in NOTES.md | One-PC half done: equal hashes through all of Corneria, Release and Debug, 30 to 360 fps. A Windows host and a Linux joining player flew the whole campaign without a desync on 2026-11-09 and 2026-11-10. **Open:** the recorded two-machine run that decision 0002 asks for before a release. |
| WP-03 | [#3](https://github.com/Clodmony/starship-coop/issues/3) | Input recording and replay (record controller input per game frame, play it back) | test | WP-01 | A recorded Corneria run replays to the same level-complete screen | Done 2026-11-06. |
| WP-04 | [#4](https://github.com/Clodmony/starship-coop/issues/4) | State hash: one hash per game frame over players, object arrays, RNG seeds and key globals | test, sync | WP-01 | Hash is logged per frame; a deliberate one-byte change shows up | Done 2026-11-06. |
| WP-05 | [#5](https://github.com/Clodmony/starship-coop/issues/5) | Single-player assumptions audit: every use of `gPlayer[0]`, `gPlayer->`, `gMainController`, `gCamCount` in campaign code | engine, research | | List with file:line and a verdict per site in NOTES.md | Done 2026-11-06. |
| WP-06 | [#6](https://github.com/Clodmony/starship-coop/issues/6) | On-rails mechanics: how player position, path progress and camera interact | engine, research | | Findings in NOTES.md with sources | Done 2026-11-06. |
| WP-07 | [#7](https://github.com/Clodmony/starship-coop/issues/7) | Decide the game rules in DESIGN.md section 4 | design | WP-05 | Decision records merged | Decisions 0005 to 0008, 0010 to 0013 and 0015 are Accepted. **Open:** 0009 and 0014 are still Proposed (the owner's answers are recorded in them; the text waits). |

Research for later records under WP-07, opened from decision 0005:

- Wingman events and routes: [#59](https://github.com/Clodmony/starship-coop/issues/59)
- Wingman radio lines: [#60](https://github.com/Clodmony/starship-coop/issues/60)
- How a downed wingman returns: [#61](https://github.com/Clodmony/starship-coop/issues/61)

## M1: Online co-op, first level

Two players on two PCs fly Corneria together, start to finish, with matching state hashes.

| WP | Issue | Title | Area | Needs | Done when | Status |
|---|---|---|---|---|---|---|
| WP-10 | [#8](https://github.com/Clodmony/starship-coop/issues/8) | Separate player count from camera count | engine | WP-05 | A `gCoopPlayerCount` (name to be agreed) drives player loops; single player unchanged (replay test) | Done 2026-11-06. |
| WP-11 | [#9](https://github.com/Clodmony/starship-coop/issues/9) | Spawn and update player 2 in all-range levels | engine | WP-10 | A second player, driven by a replay file (later by the network), flies in Corneria's all-range section | Done 2026-11-06. |
| WP-12 | [#10](https://github.com/Clodmony/starship-coop/issues/10) | Player 2 in on-rails levels | engine | WP-06, WP-10 | Player 2 flies the Corneria rail section without breaking path progress | Done 2026-11-06. |
| WP-14 | [#12](https://github.com/Clodmony/starship-coop/issues/12) | Input source per player: local controller for your own player, network or replay for the others | input | WP-10 | The local player answers only to the local controller; other players follow their input stream | Done 2026-11-06. |
| WP-28 | [#30](https://github.com/Clodmony/starship-coop/issues/30) | Local player view: camera, HUD and sound follow the client's own player, which is not always player 0 | render, ui | WP-10 | On each client the camera and HUD follow that client's player; the other player is drawn as an Arwing with a shield bar | Done 2026-11-14 (merged earlier in PR #139); the view with three other ships, the lives number and the draw order came later (#332, #335, #336, #342, in the combined build). |
| WP-16 | [#14](https://github.com/Clodmony/starship-coop/issues/14) | Game rules from WP-07: lives, down and respawn, route, score | engine | WP-07, WP-11 | Rules covered by replay tests | Built and tested by scripts in the combined build: the lives pool (3, 4 or 5 by the number of players), a life taken by any player, return and refill (decision 0007), the team's score and medals and the host's route (decision 0008), with scenarios for 2, 3 and 4 players. Not played by hand. The issue closes with PR #285. |
| WP-19 | [#17](https://github.com/Clodmony/starship-coop/issues/17) | Bosses, cutscenes, level intro and level complete with 2 players | engine | WP-11 | Corneria start to finish, both players, scripted | Done 2026-11-11. |
| WP-20 | [#18](https://github.com/Clodmony/starship-coop/issues/18) | Transport interface + SDL2_net implementation behind `USE_NETWORKING` | net | WP-01 | Two processes exchange framed messages; loss and lag tested | Done 2026-11-05. |
| WP-21 | [#19](https://github.com/Clodmony/starship-coop/issues/19) | Session: host, join, handshake (build id, ROM version, sim CVars) | net | WP-20 | Mismatches are refused with a clear message | Done 2026-11-06. |
| WP-22 | [#20](https://github.com/Clodmony/starship-coop/issues/20) | Sync model implementation per decision 0002 | sync | WP-02 (its one-PC half), WP-10, WP-11, WP-12, WP-14 | Two clients finish Corneria with matching state hashes | Done 2026-11-07. |
| WP-23 | [#21](https://github.com/Clodmony/starship-coop/issues/21) | Desync detection and report: compare hashes, dump the first diverging frame | sync, test | WP-04, WP-22 | A forced desync is caught and reported with the frame number | Done 2026-11-07. |
| WP-24 | [#22](https://github.com/Clodmony/starship-coop/issues/22) | Join flow: lobby to level start for all players together | net, ui | WP-21, WP-34 | Both players start the same level with the same seed and their chosen characters; a character already taken is refused | Done 2026-11-06. |
| WP-25 | [#23](https://github.com/Clodmony/starship-coop/issues/23) | ImGui co-op menu: host, join, status, ping | ui | WP-21, WP-34 | Usable without editing config files; a character already taken cannot be chosen | Done 2026-11-06. |
| WP-34 | [#58](https://github.com/Clodmony/starship-coop/issues/58) | Carry each player's character through the session | net | WP-21 | All clients agree on every player's character; a character already taken is refused; session tests cover both | Done 2026-11-06. |

## M2: Online co-op, whole campaign

| WP | Issue | Title | Area | Needs | Done when | Status |
|---|---|---|---|---|---|---|
| WP-17 | [#15](https://github.com/Clodmony/starship-coop/issues/15) | Wingman replacement: players 2-4 replace the chosen wingman | engine | WP-07, WP-11, WP-34 | Radio and team shields consistent, tested with two players | Done 2026-11-11. |
| WP-18 | [#16](https://github.com/Clodmony/starship-coop/issues/16) | Vehicles: Landmaster and Blue Marine levels with 2 players | engine | WP-11 | Titania and Aquas playable by script | Done 2026-11-11. |
| WP-26 | [#24](https://github.com/Clodmony/starship-coop/issues/24) | Steam relay transport (Spacewar app 480), no port forwarding | net | WP-20 | Two PCs connect through Steam | Built and tested on a fake Steam only. **Open:** two PCs with two Steam accounts. |
| WP-27 | [#25](https://github.com/Clodmony/starship-coop/issues/25) | Lag and loss testing with a chaos relay | test | WP-22 | Results with numbers in QA notes | Done 2026-11-11. |

## M3: Release

[Decision 0002](decisions/0002-sync-model.md) names two conditions before a first public release: the two-machine run
([#2](https://github.com/Clodmony/starship-coop/issues/2)), and `hash_check.py builds` and `rates` on Windows Release
([#145](https://github.com/Clodmony/starship-coop/issues/145)).

| WP | Issue | Title | Area | Needs | Done when | Status |
|---|---|---|---|---|---|---|
| WP-30 | [#26](https://github.com/Clodmony/starship-coop/issues/26) | CI artifacts named for co-op builds; version shown in game | build | | Builds downloadable from Actions | Done 2026-11-06. GitHub CI has been off since 2026-11-07; releases come from `release.yml` (#358). |
| WP-31 | [#27](https://github.com/Clodmony/starship-coop/issues/27) | Player docs: install, host, join, known issues | docs | M2 | README "How to play" section written from a real run | Done 2026-11-11 (the README's "How to play"); a release notes template is in `docs/coop/releases/`. |
| WP-32 | [#28](https://github.com/Clodmony/starship-coop/issues/28) | Full campaign co-op playthrough (scripted where possible) | test | M2 | QA level `automated` or `verified-playthrough` in QA.md | `campaign.py` flies the whole route with two real clients, and every level with three and four, equal on every frame. **Open:** a run by people (`docs/coop/TWO_PC_TEST.md`) for the level `verified-playthrough`. |
| WP-33 | [#29](https://github.com/Clodmony/starship-coop/issues/29) | Co-op Anything write-up and directory entry | docs | | PR to the hub, see HUB.md | Done 2026-11-14. **Open for the owner:** the hub pull request (cranebeaker/coop-anything#358) is a draft. |

## Before a first public release

Not work packages: a checklist, in order. The steps and the commands are in [MAINTAINING.md](MAINTAINING.md), "Releases".

| Step | State |
|---|---|
| 1. The combined test build 6 is merged into `coop` | Waits for the owner's word (PR #285) |
| 2. The full gate passes on the `coop` head, recorded in QA.md | Passed on the combined head 2026-11-14 (59 of 59); to repeat on `coop` after the merge |
| 3. The two conditions of decision 0002: a run on two real PCs ([#2](https://github.com/Clodmony/starship-coop/issues/2)) and `hash_check.py builds` and `rates` on Windows Release ([#145](https://github.com/Clodmony/starship-coop/issues/145)) | Open. The owner may cut a pre-release first (a tag with `alpha`, `beta` or `rc`); its notes then say which condition is open |
| 4. Release notes `docs/coop/releases/<tag>.md` from `TEMPLATE.md`, with the gated commit | Template written, no notes yet |
| 5. Settings, once: the ruleset `release-tags` and the environment `release` with the owner as reviewer | Open (the owner's action) |
| 6. A tag runs `release.yml`: checks, builds Linux and Windows, scans for game data, makes a draft release | Built, has not run on GitHub |
| 7. The draft is tried on a PC that has never seen the repository, then published | Open |
| 8. The nine HUD needle textures in `starship.o2r`: the owner decides ([#357](https://github.com/Clodmony/starship-coop/issues/357): no source found in the ROM; the decision of PR #120 stands) | Open |

Limits the notes must say: the Linux package needs the libraries of Ubuntu 24.04, there is no macOS build, and Windows
against Linux is a warning, not a refusal (decision 0010).

## Later

Deferred work packages keep their issues, in the GitHub milestone "Later".

| WP | Issue | Title | Area | Needs | Done when | Status |
|---|---|---|---|---|---|---|
| WP-13 | [#11](https://github.com/Clodmony/starship-coop/issues/11) | Local split screen: cameras and viewports for 2 players on one PC | render | WP-10, WP-28 | Two viewports, each following its own player | Deferred ("Later"). |
| WP-15 | [#13](https://github.com/Clodmony/starship-coop/issues/13) | Local split screen: HUD per viewport | ui | WP-13 | Both HUDs drawn in their viewports | Deferred ("Later"). |

- Three and four players beyond what is tested. Built and tested by scripts: the game layer seats up to four, the lives
  pool is 3, 4 or 5 by the number of players (decision 0007), `campaign.py --players 3|4` flies the levels
  ([#322](https://github.com/Clodmony/starship-coop/issues/322)), the rules for a hit and an aim hold with the extra
  players ([#329](https://github.com/Clodmony/starship-coop/issues/329)), and the view and the names are looked at
  ([#332](https://github.com/Clodmony/starship-coop/issues/332), [#342](https://github.com/Clodmony/starship-coop/issues/342)).
  Open: the boss, score and vehicle rule scenarios with more players ([#344](https://github.com/Clodmony/starship-coop/issues/344)),
  two questions of the rules ([#343](https://github.com/Clodmony/starship-coop/issues/343),
  [#346](https://github.com/Clodmony/starship-coop/issues/346)), and a session of three or four real PCs.
- Rollback instead of plain lockstep, if input delay hurts (decision 0002).
- Cross-platform play (Windows with Linux), if determinism holds across compilers. A platform difference warns and the
  session runs (decision 0010); the desync check shows whether the two stay in step.
- Late join and reconnect.
