# Two-PC test: fly the campaign together

The test of co-op between two real PCs. Two people did it once, on 2026-11-09 and 2026-11-10, with a Windows host and
a Linux joining player, from Corneria to the ending (README.md, "What a real run showed"). The build has changed a
great deal since: scripts fly all 17 levels with two games in lockstep on one PC, equal on every frame
(`docs/coop/QA.md`), and nobody has played the current build by hand. This page is the plan for the next run.

**What it checks:** that two games over a real network stay in step through whole levels and the map between them, how
the input delay feels, what a player sees and hears when a connection drops or the games part, and that a desync, if
one happens, is caught, repaired and reported.

## Before you start

- **The same build on both PCs.** There is no download: GitHub CI is off, its last builds are of 2026-11-07 and
  older than everything this page describes, and no release exists yet (the workflow for one is built and has not run,
  `docs/coop/MAINTAINING.md`, "Releases"). So one player builds a package (`python tools/coop/package.py --config
  Release`) and sends it to the other, or both build the same commit from source: two builds of the same commit made
  with different compiler versions count as different platforms, see the next point.
- **Windows and Linux may meet.** A Windows game and a Linux game join one session, and both see a warning that names
  the two platforms. Nobody has shown yet that the two compute the same game (decision 0010), so such a session may
  desync: the games are put in step again from the host's state, or the level starts again for both, with a report,
  see "What to send back". Two builds of one commit from different compiler versions get the same warning.
- **Check the label.** Press F1 to open the menu bar. Its right end shows `Co-op <id>`. It must be the same on both
  PCs.
- **Your own ROM.** Each player needs a Star Fox 64 US ROM, and both the same version, 1.0 or 1.1. The first start
  builds the game data from it, as Starship always does.
- **The same settings.** Settings that change the game, such as cheats like Invincible, must match. If they do not, the
  join is refused and the message names the setting.
- **A way through the network.** The host's UDP port 46464 has to be reachable. On the same home network that just
  works. Over the internet, the host forwards UDP port 46464 on their router, or both join the same virtual private
  network. A firewall may ask on the first host. Allow it.

## Steps

1. Both players start the game, press F1, and open the **Co-op** menu and its **Co-op window**. Type a name under
   **Your name** if you like (up to 8 characters); the other player sees it over your ship.
2. **Host:** leave the port at 46464 and click **Host a game**. Tell the other player your address: the local IP on a
   home network, or the public IP over the internet.
3. **Joining player:** type the host's address and the port, then click **Join a game**. The status line shows the
   join and a ping.
4. **Both:** pick a character. The host flies Fox; the other player picks a wingman.
5. **Host:** optionally pick another level in the **Start at** dropdown (Corneria unless you do), then click **Start
   the level**. Both games go to that level together (#211). A level started this way carries none of the route a
   campaign builds on the way, and the map after it is untested.
6. **Fly the level together.** Either player's Start pauses both games. If the host's save has the level cleared,
   either player's Start in the intro skips it on both games; otherwise Start does nothing there (#191). At the end
   the results stay up until both have pressed A, 30 seconds at most (#252).
7. **Go on through the map into the next level.** The map reads the save, and every game plays on the host's progress
   (decision 0009, item 8): the host sends it once before the level starts, and no PC's save file is written during a
   session. A session with a Windows host and a Linux joining player went through two maps on the evening of 2026-11-10.
8. **End the session** (Leave, or the host closes it). Each game then adds what the session earned to its own save:
   the planets it cleared and their medals, if the host's save did not have them before
   ([decision 0017](decisions/0017-what-a-session-earns.md)). The co-op window says so in one line, for example
   `Kept from this session: Corneria (medal), Meteo.`, and the co-op log has the same. No line means nothing new was
   kept: the session ended before a level's clear, or the host had those planets cleared already (then nobody earns
   them again), or your own save had them. Rankings are not kept. A game that crashes or is killed in a session
   keeps nothing, so end the session before you quit. Tested by scripts on one Linux PC only (2026-11-17), not yet by
   hand: please note what the line said on each PC.

## What is known not to work yet

- By hand on two PCs only one route has been played (Corneria to the ending, 2026-11-09 and 2026-11-10); the code merged
  on 2026-11-11 has not been played by hand. Scripts now fly all 17 levels with two clients (a test pilot that cannot
  die, and with unlimited bombs on Zoness and Venom), with equal hashes on every frame (NOTES.md, "2026-11-11, the
  campaign run").
- Lives follow [decision 0007](decisions/0007-lives-and-respawn.md). The team shares one pool, the game's own life
  count, and a life anyone collects goes into it; it starts at 3 for two players (2 plus 1 for each player after the
  first, scripted, #14), unless the host flew a level alone before the session, which brings the host's own count
  (#343). If the other player goes down, they leave the level and come back beside Fox 150 game frames (about 5 seconds) later, at full
  shields, for one life from the pool; at 0 lives they wait for a life or the next level. That return is seen in
  scripted two-client runs on Corneria only. If Fox goes down, he is refilled in place for one life from the pool (rule
  12), on the 16 levels the owner opened on 2026-11-11; scripted two-client runs only, nobody has seen it in play. With
  no life left in the pool the original rule holds: game over.
- The pause menu belongs to the host: cursor, retry and quit.
- If the connection drops, or one player sends no input for 10 seconds, both games stop where they are and the host
  waits 30 seconds for that player ("Player 2 lost the connection. Waiting for it to come back (30 s)."). Its game
  comes back by itself, and the level goes on where it stood ("Player 2 is back. The level goes on."), on the map too
  (decision 0016 part A). If its game cannot go on from there, the level starts again for both (at the last
  checkpoint, or from its start). If it does not come back, or a player leaves on purpose, the session ends for both
  (with three or four players the others start the level again). **Scripted only, with a stopped process on one PC:
  a real connection that drops is what this run is for** (#267).
- If the games go out of step (a desync), both hold for a moment and the host's state is written over the other
  game's ("The games parted at frame N. Syncing them again at frame M.", then "The games are in step again.",
  decision 0016 part B). If that cannot be done, the level starts again for everyone: at the last checkpoint if one
  was reached, as after a death but without a life lost, otherwise from its start with the host's lives, shields and
  score as they were on entering it. One line in the co-op window says so. If the games part again at once after a
  level start, twice in a row, the session ends. A repair between a Windows and a Linux game is not tried.
- No Steam yet: this is a direct connection.

## What to send back

- **If the game reports a desync:** the co-op window says "The games parted at frame N. Syncing them again at frame
  M." (or, when the repair could not be done, "The games parted. Starting the level again together."), and both co-op
  logs (below) name the frame, the level, the two players and the first value that differed, with both
  games' values: a line `first difference at frame ..., players 1 and 2: ...` (#189). One PC's logs are enough for
  that; nobody has to fetch a file from the other PC. Each game also writes a report file,
  `coop-desync-<frame>-player<n>.txt`, which holds the whole state of that frame; when the session ends with the
  connection still up, each game hands its last report to the other, and `python tools/coop/desync_report.py diff <a>
  <b>` compares two. Send the reports along if you have them. Say what each PC ran: Windows or Linux, and the
  platform line the menu's warning showed, if the two differed. A desync between two platforms is the result decision
  0010 waits for, not a surprise.
- **The co-op logs:** `coop-log-<session start>-player1.txt` and `-player2.txt` from the game folder, where its config
  and saves are. Each game writes its own and gets the other player's during the session, so one PC's folder normally
  holds both, up to the other's `session closed` line when the session ended with the connection still up; send both
  folders' files anyway, as the last lines cannot cross when a PC or its connection is gone. They
  hold the session, the ping and resends every 5 seconds, every frame held and for how long, the desync check, pauses,
  players going down, a state line per player each second, and the game's own warnings. The home folder and the user
  name are taken out before a line is written or sent. `python tools/coop/coop_log.py merge <folder a> <folder b>`
  puts them into one timeline; `--category lockstep,desync` narrows it. Set `SF64_COOP_LOG=verbose` before the
  game starts for every held frame and every hash message.
- **Logs:** `logs/Starship.log` from the game folder on both PCs. These can hold your user name in file paths, so send
  them directly, not in a public issue, unless you have checked them.
- **How it felt:** each frame waits for both players' input, with an input delay of 3 frames (about 100 ms). Did the
  controls feel late? Did the game stutter or wait? What ping did the menu show?
- **Where it went wrong:** what you did just before, and the time.

Results go on issue [#2](https://github.com/Clodmony/starship-coop/issues/2), and a run between Windows and Linux on
[#171](https://github.com/Clodmony/starship-coop/issues/171) as well, without logs or reports that hold personal data.
