# Two-PC test: fly Corneria together

The first test of co-op between two real PCs. On one PC, scripts have flown all of Corneria with two games in lockstep,
equal on every frame (`docs/coop/QA.md`). Nobody has done it on two PCs, or with people at the controls. This page
is the plan for that run.

**What it checks:** that two games over a real network stay in step through a whole level, how the input delay feels,
and that a desync, if one happens, is caught and reported.

## Before you start

- **The same build on both PCs.** Download one CI build from the repository's Actions page: the newest green
  **Co-op build** run on `coop`, artifact `starship-coop-<id>-<os>-x64`. Both players take the artifact of the same
  run. Downloading needs a GitHub login, and artifacts expire after 14 days.
- **The same operating system.** A Windows game and a Linux game refuse each other, as nobody has shown yet that they
  compute the same game (decision 0002, platform rule). Both on Windows or both on Linux.
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

1. Both players start the game, press F1, and open the **Co-op** menu.
2. **Host:** leave the port at 46464 and click **Host a game**. Tell the other player your address: the local IP on a
   home network, or the public IP over the internet.
3. **Joining player:** type the host's address and the port, then click **Join a game**. The status line shows the
   join and a ping.
4. **Both:** pick a character. The host flies Fox; the other player picks a wingman.
5. **Host:** click **Start the level**. Both games go to Corneria together.
6. **Fly the level together.** Either player's Start pauses both games. Intro skip is off in a session.
7. **Stop at the map** after the level is complete, and quit. The map is not tested yet: each player has their own
   save, and the map reads it.

## What is known not to work yet

- Only Corneria is tested. In Landmaster and Blue Marine sections the second player is parked.
- There are no co-op rules for lives yet. If Fox goes down, the level restarts for both players. If the other player
  goes down, they stay out until the next level.
- The pause menu belongs to the host: cursor, retry and quit.
- If one player leaves or stops sending input for 10 seconds, the session ends for both.
- No Steam yet: this is a direct connection.

## What to send back

- **If the game reports a desync:** it says so on screen, names the frame and what differs, and tells you where its
  report file is: `coop-desync-<frame>-player<n>.txt`. Keep the file from **both** PCs.
  `python tools/coop/desync_report.py diff <a> <b>` compares the two.
- **Logs:** `logs/Starship.log` from the game folder on both PCs. Logs can hold your user name in file paths, so send
  them directly, not in a public issue, unless you have checked them.
- **How it felt:** each frame waits for both players' input, with an input delay of 3 frames (about 100 ms). Did the
  controls feel late? Did the game stutter or wait? What ping did the menu show?
- **Where it went wrong:** what you did just before, and the time.

Results go on issue [#2](https://github.com/Clodmony/starship-coop/issues/2), without logs or reports that hold
personal data.
