# Wire protocol

Status: the transport (WP-20), the session handshake (WP-21), the characters (WP-34), the level start (WP-24), the
session's save (decision 0009 item 8), lockstep (WP-22), the desync check (WP-23), the co-op log (#172), the handover
(#189), the recovery (a session that survives a lost player or a desync) and the dump of a desync frame (#189) exist;
`COOP_PROTOCOL_VERSION` is 9. Other sync and game messages come
with their work packages. This page sets the rules so parallel work does not collide.

## Rules

1. **Every message has an id from the registry below**, reserved in the same PR that adds the message. Ranges per
   area keep parallel branches from picking the same number; take the next free id in your area's range.
2. **Versioned handshake.** The first exchange carries a protocol version, the build id (git commit), the ROM version
   (the game version of each loaded archive), the values of the simulation-affecting CVars and the platform (OS,
   CPU, compiler with its version). A different protocol version, build, ROM or CVar refuses the connection with a
   message the player can read. A different platform only warns, on both sides, with both platforms named
   ([decision 0010](decisions/0010-cross-platform-sessions.md)): lockstep needs every client to compute the same game,
   nobody has compared two platforms, and the desync check (WP-23) is what shows whether two do. A refusal names the
   platform too when it differs as well. Bump `COOP_PROTOCOL_VERSION` whenever a message layout changes.
3. **Explicit layout.** Little-endian, fixed-size integer types, no raw `memcpy` of game structs (they hold pointers
   and padding). Write a serializer per message.
4. **Frame-stamped.** Messages that belong to a game frame carry that frame number.
5. **No game data on the wire** beyond what the protocol needs (no asset or ROM bytes).
6. Document each message here: id, name, direction, fields, when it is sent.

## Transport over UDP

Decided in [0004](decisions/0004-transport.md); the code is in `src/port/coop/net/`. A datagram holds one message and
at most 1200 bytes, so a payload holds at most 1176. Larger messages are refused, there is no fragmentation.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | CRC-32 of everything after this field |
| 4 | 2 | Magic `0x4353` |
| 6 | 1 | `COOP_PROTOCOL_VERSION` |
| 7 | 1 | Flags. Bit 0: reliable lane |
| 8 | 2 | Message id |
| 10 | 2 | Payload size |
| 12 | 4 | Connection id, the nonce in the client's connect request |
| 16 | 2 | Sequence number of this message on its lane |
| 18 | 2 | Ack: the first reliable sequence number still missing here |
| 20 | 4 | Ack bits: bit n set means sequence number ack + 1 + n has arrived |
| 24 | size | Payload |

The first 16 bytes keep their meaning in every version, and so does the refuse message, so that two versions can tell
each other apart. A datagram with a bad checksum, a bad magic or a size that does not match is dropped unread.

- **Reliable lane:** ordered, delivered exactly once. Up to 128 messages in flight per direction; after that `send`
  returns false. A message is resent until acknowledged, first after the measured round trip plus margin (50 to
  1000 ms), then at twice that delay each time, at most every 1000 ms.
- **Unreliable lane:** may be lost or arrive out of order, never delivered twice. Sequence numbers count separately,
  and only serve to drop duplicates. Something 64 or more behind the newest number counts as a duplicate.
- **Connecting:** the client repeats `CONNECT_REQUEST` every 200 ms until the host accepts or refuses, or the timeout
  passes. The host keeps peers apart by address and connection id. A client ignores the sender's address and goes by
  the connection id, because a host with several addresses may answer from another one.
- **Staying connected:** both sides ping every 250 ms, which also measures the round trip. A connection that hears
  nothing valid for 5000 ms ends with a timeout. A goodbye is sent three times and not acknowledged.
- **A connection the host no longer holds:** a host answers a datagram of a connection it does not hold (any message
  but a connect request or a goodbye) with one `DISCONNECT` for that connection id, and counts it in
  `strayDatagrams`. So a client the host dropped while the client could not hear, for example one that sent no input
  for too long, ends as closed with its next datagram and not by its own time-out, and can come back (Recovery). A
  stray goodbye gets no answer.
- **Not provided:** encryption, peer authentication, IPv6.

## Transport over Steam

Decided in [0004](decisions/0004-transport.md): the same interface on Steam's own reliability, without our layer. The
code is `src/port/coop/net/transport_steam.c`, in the game with `-DUSE_STEAM_RELAY=ON` (off by default, no SDK needed;
nothing in the game calls it yet). Peers are SteamIDs, frames go through `ISteamNetworkingMessages` on channel 0, as
app 480. The ids are the ones in the registry, with a shorter header. There is no checksum: a damaged frame is left to
Steam, and every frame is still read as untrusted. A frame holds at most 16 + 1176 bytes.

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | Magic `0x4353` |
| 2 | 1 | `COOP_PROTOCOL_VERSION` |
| 3 | 1 | Flags. Bit 0: reliable lane |
| 4 | 2 | Message id |
| 6 | 2 | Payload size |
| 8 | 4 | Connection id, the nonce in the client's connect request |
| 12 | 2 | Sequence number of this message on its lane |
| 14 | 2 | Ack: how many reliable messages have arrived here |
| 16 | size | Payload |

The first 12 bytes keep their meaning in every version, and so does the refuse message. A frame with a bad magic or a
size that does not match is dropped unread and counted in `badDatagrams`.

The lanes promise what they promise on UDP. What differs, and why:

- **Reliable lane:** Steam resends and orders. Sequence numbers only check that: a duplicate is dropped, and a hole
  ends the connection with a timeout, because a session that restarted under us has lost messages for good. Up to 128
  messages in flight per direction, then `send` returns false; so it does when Steam's own queue is full.
- **Ack:** the count rides on every frame, so `stats.pending` means "has not arrived yet", which `session.c` waits for
  before it drops a refused client. `ACK` goes out alone when there is nothing else to send. It may be lost: the next
  frame repeats it, a ping at the latest.
- **Unreliable lane:** Steam can lose or reorder a message, and can also deliver it twice ("The same unreliable message
  may be received multiple times"), so duplicates are dropped here exactly as on UDP.
- **Which lane:** `CONNECT_REQUEST`, `CONNECT_ACCEPT`, `CONNECT_REFUSE` and `DISCONNECT` go on the reliable lane.
  Unreliable frames can overtake reliable ones and nothing resends what a client dropped, so the accept has to stay
  ahead of the host's first message, and the goodbye behind its last. `PING`, `PONG` and `ACK` are unreliable.
- **Connecting:** the client repeats `CONNECT_REQUEST` every 1000 ms until the host accepts or refuses, or the timeout
  passes: `timeoutMs`, or 20000 ms if that is 0, because Steam needs time to find a route. The host accepts every
  session, or nobody new could reach it, and keeps peers apart by SteamID and connection id. A client accepts only its
  host. Steam names the sender, so a client goes by the SteamID as well as the connection id. Connecting to an empty
  SteamID or to your own fails at once: one user cannot talk to itself.
- **Staying connected:** as on UDP, a ping every 250 ms and a timeout after 5000 ms of silence. Steam says nothing when
  a peer closes on purpose, and a session that broke stays broken until it is closed. So a link also ends with a
  timeout when Steam reports the session failed, or when a send finds it gone, and the session is then closed. That
  happens after everything that had arrived has been read, so the end still comes after the last message.
- **Goodbye:** sent once, on the reliable lane, and the session stays open: closing it would throw the goodbye away.
  Steam ends unused sessions by itself.
- **A connection the host no longer holds** gets no answer here, unlike on UDP: a dropped client ends by its time-out.
- **Not provided:** lobbies and invites. The client needs the host's SteamID.

## Session handshake

The code is in `src/port/coop/session/`. All its messages go on the reliable lane.

1. When the transport reports the connection, the client sends `SESSION_HELLO` with its identity. A client that comes
   back to a session it was in sends `SESSION_REJOIN` in its place (Recovery).
2. The host compares it with its own. On a match it answers `SESSION_WELCOME`, otherwise `SESSION_REFUSE`. Both
   carry the host's identity, so the client builds the same text the host shows: what differs, host value against
   joining player value. The welcome also carries the **session id**: a random 64-bit number the host makes for each
   session it opens, outside the simulation (`src/port/coop/session/session_id.cpp`: the system's random source and
   two clocks, never the game's seeds), never 0. It tells this session from any other on the same host and build. It
   is no authentication; the protocol has none.
3. A refused client closes. The host drops it when the refusal is acknowledged, when the client goes, or after 3000 ms.
4. Nothing but the hello passes before the welcome. Other messages from a client that has not joined are dropped and
   counted. The host refuses a client that sends no hello within 10000 ms; the client gives up when the host does not
   answer within 10000 ms.

**Identity**, in this order:

| Size | Field |
|---|---|
| 1 + n | Build id: length n (below 48), then n bytes of printable ASCII. The git commit, `-dirty` with uncommitted changes. |
| 1 + n | Platform, same form, for example `Windows x86-64 MSVC 1944` |
| 1 + 4 r | Count r (at most 8), then the game version of each archive that has one, sorted. It is the ROM's header CRC as Torch writes it into `sf64.o2r`. |
| 4 | FNV-1a hash of the names in the simulation CVar table, in order |
| 2 + 4 c | Count c (at most 160), then each CVar's value, in table order. Floats as their bit pattern. |

The CVar table is `src/port/coop/session/sim_cvars.c`; why each CVar is there is in NOTES.md (WP-21).

## Characters

Who flies what, from [decision 0005](decisions/0005-players-and-characters.md). The code is in
`src/port/coop/session/session.c`. All three messages go on the reliable lane, after the welcome. The host holds the
roster and is the only one that changes it; a client's copy is what the host last sent.

**Roster**, four bytes, one per player: player 0 is the host, player n the client at transport peer n - 1. So the slot
is the player index in `SESSION_WELCOME`.

| Value | Slot |
|---|---|
| 0 | Free |
| 1 | A client that joined and holds no character yet |
| 2 | Fox: player 0, and only player 0 |
| 3 | Falco |
| 4 | Slippy |
| 5 | Peppy |

Values 2 to 5 follow the order of `TeamId` (`include/sf64player.h`).

1. The host sends `SESSION_ROSTER` to every client that has joined, whenever the roster changes: a client joined, a
   pick was taken, a client left. A new client's first one follows its welcome. Until then a client has no roster.
2. A client sends `SESSION_PICK` with the character it wants, in place of what it holds. A client that has joined holds
   nothing (value 1) until then; the host gives out nothing by itself.
3. A character someone else holds, Fox included, gets `SESSION_PICK_REFUSE` to that client only. The roster stays as it
   was, the client stays connected and may pick again. A free character goes to the client, its old one is free, and
   the roster goes out. A client that picks what it already holds gets no answer, because nothing changed.
4. Before the level a client that leaves, for whatever reason, frees its slot and its character at once. Nothing is
   kept for it: if it comes back it is a new client, which may find the character taken. In a level a client whose
   connection was lost keeps both for a while (Recovery).
5. The host orders picks by arrival, so of two picks of one character the first one wins.
6. If the transport refuses a send because its window is full, the host tries again on the next poll, with the roster
   of that moment and the last refusal. A client can miss a roster or a refusal that way, never the last one.

What does not fit, as in the handshake:

- A message out of place is dropped and counted (`CoopSession_Ignored`): a pick before the welcome or from a refused
  client, a roster or a refusal before the welcome, any of the three in the wrong direction.
- `SESSION_PICK` that cannot be read (size other than 1, or a value other than 2 to 5) ends that client as a hello
  that cannot be read would: the host says goodbye and tells its game the client left with `BAD_MESSAGE`. The other
  clients see a roster without it. Its side only sees the host close, because a joined client takes no
  `SESSION_REFUSE`.
- A client ends with `BAD_MESSAGE` on a `SESSION_PICK_REFUSE` of a size other than 1 or a value other than 2 to 5, and
  on a `SESSION_ROSTER` of a size other than 4, without Fox at player 0, with Fox or a value above 5 anywhere else,
  with a wingman twice, or with its own slot free.

Nothing here reads or writes game state. Until WP-17 the character changes nothing in the level. Picks stop at the
level start, see below.

## Level start

The host starts one level for everyone. `SESSION_LEVEL_START` goes on the reliable lane, after the welcome. The code is
in `src/port/coop/session/session.c`; what the values are is in `src/port/coop/game/level_start.c`.

1. The host's game asks for a level with what it holds now, its save included. The host turns that down while a client that joined holds
   value 1, or while a client is in the handshake, which ends within 10000 ms. Nothing changes then, and it can ask
   again. So a client never flies with nothing: nobody starts until it has picked or left.
2. When the host takes it, the level, the phase, the values, the roster and the host's save are fixed. A client that leaves afterwards does not change what the others start from, so every game comes
   to the same player count.
3. From then on nobody picks and nobody new joins. A `SESSION_PICK` on its way is dropped and counted; the client learns
   what it holds from the level start. A hello gets `SESSION_REFUSE` with bit 9, whatever its identity. A player of the
   session whose connection was lost can come back (Recovery).
4. The host sends `SESSION_SAVE`, then `SESSION_LEVEL_START`, to every client that joined, on the next poll, and tries
   again on the next one while the window is full; the level start never goes ahead of the save. Its own game hears
   of it on that poll too. Each game then enters the level from the message, the host's as well, so no game goes by
   its own history.
5. The save: see "Session's save" below. Start in a level's intro reads the save (`normalClear` of the level,
   `src/engine/fox_play.c`, #191), and every game holds the host's from here on, so any player's Start skips an intro
   the host has cleared, in every level of the session. Until version 7 a cleared flag at the end of this message
   stood in for it, for the first level only.

| Size | Field |
|---|---|
| 1 | Level: a `LevelId` below 20, so not versus |
| 1 | Phase: `gLevelPhase` |
| 1 | Run: which level start of the session this is. 1 for the first, one more each time the session starts a level again (Recovery); after 255 comes 1, never 0 |
| 4 | Roster, as in Characters |
| 4 v | The values, each a little-endian 32-bit integer, in the order of the table in `level_start.c`: `COOP_LEVEL_START_VALUES`, 182 now. Values 88 to 171 are what a campaign builds from level to level (the level's hit count, the total, the mission number, the planets flown with their hits, medals and team, the map's paths, the mission status, the clear flags): a level start in the middle of a campaign brings them along. Value 172 is 1 when the level starts at a checkpoint, and values 173 to 182 are that checkpoint (the saved object index, hits, ground surface, all-range checkpoint, searchlight status, path progress as a float's 32 bits, and each player's own hits); all 0 otherwise (Recovery) |

Each game goes to the level by itself, as soon as it hears, and what holds them together from there on is the first
frame of lockstep, below.

What does not fit, as before:

- A second level start that `SESSION_RECOVER` did not announce, one before the welcome, and one from a client are
  dropped and counted (`CoopSession_Ignored`).
- A client ends with `BAD_MESSAGE` on a level start of another size, with a level of 20 or more, with run 0, with a
  roster that `SESSION_ROSTER` could not hold, with value 1 anywhere in the roster, with no `SESSION_SAVE` ahead of
  it, or, after its first one, with a run that is not the next (a client that came back takes any run as its first).

## Session's save

[Decision 0009](decisions/0009-vehicles-cutscenes-save.md) item 8: the host's progress is the save of the session, and
no machine writes a real save while the session runs. `SESSION_SAVE` goes on the reliable lane, from the host to each
client, just ahead of every `SESSION_LEVEL_START` (above, rule 4): once for the first level, and again each time the
session starts a level again (Recovery), with the host's progress as it stood when the host entered that level. The
session code passes it on as bytes; the game
takes it in `src/port/coop/game/save.c`.

| Size | Field |
|---|---|
| 256 | The host's `Save` (`include/sf64save.h`) as its file holds it: 254 bytes of `SaveData`, then `Save_Checksum` of them, low byte first |

1. Every game, the host's too, takes the host's progress into its save in memory when it enters the level: the planet
   bytes, the rankings and the bytes after them (`padEE`, whose first byte says the ending was seen). It keeps its own
   sound mode and volumes (offsets 0x14 to 0x17) and the two language bytes (0xEB, 0xEC), and works out the checksum
   again. Those settings are no simulation; the state hash leaves them out and hashes the rest, in region `globals`
   (`gSaveFile.planet`, `gSaveFile.ranking`, `gSaveFile.padEE`), so a copy that drifted is a desync with its name.
2. From then on no game writes its save file. Every `Save_Write` of the session runs on every game on the same frame,
   so the copies stay equal without another message.
3. When the session ends, however it ends, each game reads its own save back from its file. What the session earned
   is not kept.

What does not fit:

- A save of another size ends the client with `BAD_MESSAGE`. A second save before the level start, a save after it that
  `SESSION_RECOVER` did not announce, one before the welcome and one from a client are dropped and counted
  (`CoopSession_Ignored`).

## Recovery

The session survives a lost player or a desync (the owner's rules of 2026-11-11). One message cannot repair games that
have parted: their worlds differ in actors, random numbers and the audio clock, and lockstep sends only inputs. So the
recovery point is a level start: the level ends for everyone, the host sends its save and a level start again, and every
game enters the level from the host's state. The parted level is lost; the session, its players and their characters
stay. The code is `src/port/coop/session/session.c` ("Recovery" in `session.h`), and `lockstep_game.c`, `menu.c` and
`menu_game.c` on the game's side.

**What starts again.** The level the team is in. If the host's game reached a checkpoint in it, the level starts
again there, as it does after a death but with no life taken: with the host's values of the moment lockstep stopped,
and its checkpoint (the saved path progress, object index, ground and searchlight on rails; the all-range checkpoint
of Fortuna, Katina, Bolse and Andross; the hits saved there). Otherwise it starts from its start, with lives, shields,
lasers, bombs, score and the campaign's state as the host had them on entering it: the host keeps that level start
(the **restart point**), taken on the frame its game goes into a level by itself (the map's next level, a level's next
part), and the session's own first level start before that. If the games part on the map after a level, that level is
flown again, from its last checkpoint if a recovery had already started it there.

The checkpoint travels in the level start and is put in place one frame after a game entered the level, because
entering clears it (`Play_Setup`, `src/engine/fox_play.c:530`); the level's first setup, a few frames later, reads it
(`CoopLevelStart_AfterEntry`, `src/port/coop/game/level_start.c`). Not carried: what Venom 2's second part and Andross
keep of Fox beside the checkpoint (`D_ctx_80177C9C` and the values next to it, `src/engine/fox_play.c:4698`); each
game restores those from its own, as the game does.

**A recovery that does not help** ends the session as before there was one: when a desync is complete within 300
frames of a level start for the second time in a row (`SYNC_QUICK_PARTS`, `lockstep_game.c`), every game ends its
session with the desync's text and "The level started again and the games parted again at once". A client on
another save, for example, parts at frame 0 of every level start.

**In a level, on the host:**

1. A client whose connection timed out, or that sent no input for `COOP_LOCKSTEP_STALL_MS` (the host then drops it,
   `CoopSession_Kick`), keeps its slot and its character for `COOP_SESSION_REJOIN_MS`, 15000 ms. A client that says
   goodbye is gone at once: it left on purpose.
2. Whenever a client goes, and when a desync is complete (Desync check), the host sends `SESSION_RECOVER` to every
   client that is there, and its own game stops lockstep. Each game shows one line in the co-op window, which comes up
   by itself: "Player 2 lost the connection. Waiting for it to come back (15 s), then the level starts again.",
   "Player 2 left. Starting the level again together." or "The games parted. Starting the level again together."
3. Once nobody is waited for (the lost player is back, or its 15 s are over and its slot is free), the host sends
   `SESSION_SAVE` and `SESSION_LEVEL_START` with the restart point, the next run number and the roster of that moment,
   to everyone who is there, and enters the level itself. A connection in its handshake is waited for, as it may be the
   player coming back; that ends within 10000 ms.
4. With no client left and nobody waited for there is nothing to start again: the session ends as it always did, and
   the level goes on in the host's game alone.

**A client:**

5. `SESSION_RECOVER` (and a desync the client completed itself) stops lockstep; the level goes on in that game alone
   until the level start comes. From here on a save and a level start are taken again, once. If none comes within
   `COOP_SESSION_RECOVER_MS`, 40000 ms, the client ends its session ("The host did not start the level again in time").
6. A client that waits for another player's input leaves that to the host for 40000 ms (`SYNC_CLIENT_STALL_MS`),
   where it used to end the session after 10000 ms: the host drops the silent player, or the connection to the host
   times out.
7. A client whose connection to the host ends in a level without the host saying that the session is over (a
   time-out, or a goodbye that no handover announced) opens a new connection to the same address and sends
   `SESSION_REJOIN`. If it reaches nobody within the transport's connect time, or is refused, its session ends as
   before, with the text of how it lost the host.

`SESSION_RECOVER`, host to client, in a level:

| Size | Field |
|---|---|
| 1 | Reason: 1 a player's connection was lost and is waited for, 2 a player left or was dropped, 3 the games parted |
| 1 | The player it is about (index 1 to 3), 0xFF for nobody |

`SESSION_REJOIN`, client to host, in place of the hello:

| Size | Field |
|---|---|
| 8 | The session id of the welcome the client got, little-endian |
| 1 | The character it flew at its last level start (3 to 5) |
| n | Its identity, as in the hello |

8. The host compares the identity as for a hello, then the id: another id, or a session without one or before its
   level, gets `SESSION_REFUSE` with bit 10 ("The host is not running the session this game was in"). A character
   nobody is waited for gets bit 9, as any late client.
9. A player who is waited for gets `SESSION_WELCOME`. It is the player of the transport peer it came on, so its index
   can be another one than before; its character moves with it, and if that slot was kept for another player who is
   away, the two change places. The roster goes out, and the level starts again as in 3.
10. If the character is held by a client the host still counts as connected, the host drops that connection (the
    player says it is dead) and goes on as in 9. So a client that noticed the loss before the host did gets back in.
11. A client that comes back takes the welcome only if it carries the id it sent: a host that restarted has another.

What does not fit:

- `SESSION_RECOVER` before a level is dropped and counted; one of another size, with a reason other than 1 to 3 or a
  player other than 0 to 3 or 0xFF ends the client with `BAD_MESSAGE`.
- `SESSION_REJOIN` shorter than its head or with an identity that cannot be read is refused with bit 8; one from a
  client that has joined is dropped and counted.

Not built: coming back inside a level without a restart (the inputs a client missed are gone after 64 frames), a
state snapshot from the host (decision 0002, option C), a new player joining a level that runs, and a client coming
back after its game was restarted (the session id lives in memory).

## Lockstep

[Decision 0002](decisions/0002-sync-model.md): every client runs the whole game, and only inputs travel. The code is
`src/port/coop/sync/lockstep.c`, and `lockstep_game.c` connects it to the game. A session of one player has no lockstep.

**Frames.** Frame 0 is the first frame in `PLAY_UPDATE` after a level start, on every client. From then on every game
frame is a lockstep frame, on the map too, until the session ends or starts a level again (Recovery), where the count
starts at 0 again.

**Run.** Every sync message (`SYNC_INPUT`, `SYNC_HASH`, `SYNC_DESYNC`) carries one byte in front of the fields given
below: the run number of the level start it belongs to. A message of another run is dropped and counted, never the
end of anything: what was sent in the level before a restart is not for the level after it. Frame F runs on a client once it holds every
player's input for F. Without them nothing of the frame runs, no game frame and no audio frame, and the last frame is
drawn again so the window stays alive. Each client counts by itself; no message says what frame anyone is on.

**Input.** What a client's local controller did on its frame F is that player's input for frame F + delay. The delay is
`COOP_LOCKSTEP_INPUT_DELAY`, 3 frames, 100 ms at the game's 30 frames a second, and fixed for a session. A player's first
`delay` frames are empty and go out like all others, so nobody runs frame 0 before everyone has reached it. An input is
the pair the sender's player used, as in `src/port/coop/game/input.h`: `hold` and `press`.

**Who sends what.** Clients only talk to the host. A client sends its own frames, and the host passes each player's frames
on to everyone else, so the host's own input takes one hop and another client's takes two.

`SYNC_INPUT` goes on the unreliable lane, because a resend timer would hold every frame behind a lost datagram. Each
message repeats everything the other end has not confirmed. It goes out once per frame and when something new came in
for another client, and at least every `COOP_LOCKSTEP_RESEND_MS`, 30 ms, also while a frame waits.

| Size | Field |
|---|---|
| 4 x 4 | For each of the 4 players, the first frame the sender does not hold: this confirms everything before it |
| 1 | Blocks b |
| b x | Block: u8 player, u32 first frame, u8 count c (1 to 32), then c frames |

A frame is 8 bytes, hold then press, each as u16 buttons and s8 stick x and y. All numbers are little-endian. A receiver
keeps what it lacks and skips what it holds, so a block may overlap what came before, and a repeated or late datagram
changes nothing.

**Pause.** Any player can pause, by the owner's decision on #12. There is no message of its own: a pause is a Start press
in `SYNC_INPUT`, and that already reaches everyone stamped with its frame. Every client tests every player's press on that
frame (`CoopPlayers_PausePressed`), so all of them pause on the same frame, and Start from any player resumes on one. A
message apart from the input would need its own way of agreeing on a frame. What the pause menu does next stays on player
0: its cursor reads player 0's pad, as do retry and quit. The other team-wide inputs stay on player 0 too.

**End.** Every frame needs every player, so a level cannot go on when one player goes. The session starts it again
for those who stay (Recovery), and ends for everyone where nobody is left to start it with. Then the level goes on
in each game, with the local player on its controller and the others idle: that is the choice `Leave` already had
(#126).

- A client that leaves or times out: the host starts the level again with the other clients, or waits for the lost
  one. With no other client the host ends the session, and says who left.
- The host that leaves: the client sees the session end. The host whose connection times out: the client tries to
  come back, and its session ends when it reaches nobody.
- `COOP_LOCKSTEP_STALL_MS`, 10000 ms, with no frame and no input from a player, on a connection that stays up: the
  host drops that player as a lost one and names it; a client leaves that to the host for 40000 ms. Before frame 0 it
  is `COOP_LOCKSTEP_START_MS`, 60000 ms, as a slow PC may still be loading.
- The player reads why in the co-op window, which comes up by itself. So it does while a frame has waited for more than
  a second.

What does not fit:

- `SYNC_INPUT` of the wrong size, for a player who is not in the level, from a client for anyone but itself, to a client for
  itself, with a count of 0 or above 32, with a gap, or reaching further than `COOP_LOCKSTEP_RING` frames ahead of the
  frame that runs next: the receiver ends the session.
- Before this game has reached frame 0, a `SYNC_INPUT` is dropped, and it comes again.

Rollback is under "Later" in ROADMAP.md.

## Desync check

WP-23, [decision 0002](decisions/0002-sync-model.md): every client hashes its state on every lockstep frame, before the
frame runs (`src/port/coop/sync/state_hash.c`, the same hash the test tools log), and the clients compare. The code is
`src/port/coop/sync/desync.c`, and `desync_game.c` connects it to the game, from `lockstep_game.c`. Nothing here writes
game state, reads a clock inside the simulation or holds a frame: a late or lost hash leaves a frame unchecked, never
held. Co-op off runs no hash, and a session with no desync hashes as before.

**Hashes.** The hash of frame F is the state after frame F - 1, as in the hash log. Each game keeps its last 64 frames
(`COOP_DESYNC_RING`), with the hash of every region and a dump of the bytes hashed. Every `COOP_DESYNC_PERIOD`, 10,
frames it sends `SYNC_HASH` with the totals of the last 10 frames to the games it talks to: a client to the host, the host
to every client. So a client is compared with the host, and the host with each client, as inputs travel. Why 10: hashing
is per frame, as the decision says, and the message is what has a cost. One message a frame would double lockstep's
datagram rate for 8 bytes of payload; every 10 frames the totals go out in one datagram, and a desync is still named
within a third of a second plus a round trip. 64 frames of ring hold what the slowest client can still ask about: a peer
at most `COOP_LOCKSTEP_MAX_DELAY`, 16, frames ahead when its batch leaves, plus the batch, plus a resend or two.

`SYNC_HASH` goes on the reliable lane, in order, so no batch is lost and the first differing frame is the first one. A
receiver compares every total with its own for that frame. A frame it has not run yet waits in the message, four messages
deep per peer; a frame it no longer holds counts as unchecked, never as a desync.

| Size | Field |
|---|---|
| 4 | First frame |
| 1 | Count c (1 to 32) |
| 8 c | The total hash of each frame, little-endian u64 |

**The desync.** The first frame whose totals differ is the desync, found by both sides of it: the host from the client's
batch and the client from the host's, and whichever games find it, or hear of it, send `SYNC_DESYNC` with their own
hash of that frame, region by region, on the reliable lane. A game that receives one before it found the desync itself
takes the frame and the pair from it, as a third player does. For a game in the pair the desync is complete once the
other side's hash has come, or `COOP_DESYNC_GRACE_MS`, 1000 ms, after its own went out; a game outside the pair waits
the grace out. Then the game writes its report and its co-op log names the frame, the level and its mode, the two
players, the regions and the report's path, and the session starts the level again (Recovery).

| Size | Field |
|---|---|
| 4 | Frame |
| 1 | Sender's player index |
| 1, 1 | The pair: the two players whose totals differed, in order |
| 1 | Flags: bit 0, the sender still held the frame, so its hashes below are real |
| 4 | The sender's game state, level, level mode and play state at that frame, u8 each |
| 8 | The sender's total hash |
| 8 x 12 | One hash per region, in the order of `CoopHashRegion` |

**The report** is `coop-desync-<frame>-player<n>.txt` in the game's own folder, where its config and saves are, one per
game: both games' identity as the handshake had them (build, platform, ROM, CVar count), the frame, the pair, the regions
that differ, both sides' hashes, and a dump of this game's state at that frame as hashed, pointers normalised, in named
chunks (`gActors[59]`, `gPlayer[1]`, `gLifeCount[0]`). `python tools/coop/desync_report.py diff <a> <b>` reads two and
names the first value that differs. Nothing personal goes in. The layout is `src/port/coop/sync/desync_report.c`.

**The first field that differs (#189).** Once the desync is complete, each game of the pair sends the other its dump of
the frame, the bytes its hash ran over, as `SYNC_DUMP` on the reliable lane, at most 16 parts per frame. The game that
holds both finds the first difference as `desync_report.py diff` does (regions in order, in each the chunks in order,
the first chunk whose bytes differ) and writes one line, category `desync`, into its co-op log and behind its report
as a comment:

`first difference at frame 2000, players 1 and 2: players: gPlayer[1] differs in 1 of 1264 bytes, first at +116: b3 vs
b2; word at +116: 0x444844b3 vs 0x444844b2`

The lower player's value comes first, so both games write the same line, and the log's stream puts each into both
folders. A game that held no dump of the frame says so in one message, and the line then names it; if the other dump
does not come within `COOP_DESYNC_DUMP_MS`, 8000 ms, or the session goes first, the line says that. A third game sends
and compares nothing. The exchange names its own run and frame, so it goes on while the level starts again. The code is
`src/port/coop/sync/desync_dump.c`, connected in `desync_game.c`.

| Size | Field |
|---|---|
| 1 | Run of the level start the desync was in (not a prefix as on the other sync messages: this is the message's own) |
| 4 | Frame |
| 1 | Sender's player index |
| 2 | Part, from 0 |
| 2 | Parts; 0 means the sender holds no dump of that frame |
| 4 | Size of the whole stream, at most 2 MiB |
| up to 1150 | This part's bytes: every part but the last is full |

The stream is, for each chunk of the dump in the order hashed: u8 region (`CoopHashRegion`), u8 name length n (at
most 96), the name, s32 index (-1 for a single value), u32 size, the bytes.

**What the session does after a desync: it starts the level again** (Recovery), since 2026-11-11. Until then it ended:
playing on would have each player in a different game that looks like one, and there is no repair inside a level until
host correction (option C of decision 0002) exists. The check is as strict as it was: every frame, every region.

What does not fit:

- `SYNC_HASH` of the wrong size, with a count of 0 or above 32, on a link nobody is on; `SYNC_DESYNC` of the wrong size,
  from a client for anyone but itself, with a pair of one player or of a player who is not in the level: the receiver
  ends the session.
- Before this game has reached frame 0, both are dropped.
- `SYNC_DUMP` of another run, frame or player than the receiver's own desync names, shorter than its head or longer
  than 1164 bytes, is dropped. A stream above 2 MiB, a part count that does not fit the size, a part out of order or
  of the wrong size is refused: nothing more is taken, and the line says that the dump could not be read. Neither ends
  anything.

## Co-op log

#172: every game writes a log of the session into its own folder, `coop-log-<session start>-player<n>.txt`, and streams
its lines to every other player, so every folder ends up with one file per player. The code is `src/port/coop/log/`;
`session.c` drives it from its poll, and `tools/coop/coop_log.py` merges the files into one timeline. A line is
`YYYY-MM-DD HH:MM:SS.mmm <lockstep frame or -> <level or -> <category> <text>`.

`DIAG_LOG` goes on the reliable lane, after the welcome, and nothing waits for it:

- A client sends its own lines to the host; the host writes what it gets into that player's file and passes it on to
  its other clients with the origin byte of the sender, so a client takes the origin from the message and a host from
  the peer it came from. A batch holds whole lines of one origin, at most 1176 bytes in all.
- At most one batch per peer every 50 ms (`COOP_LOG_SEND_MS`), and none while that peer has 32 or more reliable messages
  in flight (`COOP_LOG_MAX_PENDING`): the window is 128, and `SYNC_HASH` and the session's own messages come first. So a
  slow or lost peer costs log lines, never a frame.
- Lines queued for a peer wait in 32 KB per peer. A line that finds the queue full is dropped and counted, and the next
  batch that leaves is followed by a line that says how many; what is still queued when the session closes gets one more
  send and is then dropped, counted in the `log closed` line.
- A batch that cannot be read (size, origin out of range, the receiver's own index as origin) is dropped and counted,
  never the end of a session: a log is not worth one. Bytes outside printable ASCII become `?`.
- Before a line is written or sent, the home folder becomes `~`, the user name `<user>` and any IPv4 address `<ip>`.
  Nothing in the log names a peer's address: a player is "player 2".

## Handover

#189: before a game closes a session, it hands its desync report and the rest of its co-op log to every player still
connected, and takes theirs, so every folder ends with every player's report next to every player's log and
`desync_report.py diff` runs in one folder. The rules are `src/port/coop/sync/handover.c`; `handover_game.c` connects
them to the session, from `menu_game.c` and the test join driver. `HANDOVER` goes on the reliable lane of the session's
own connection, and its first byte is the kind:

| Kind | Size | Fields |
|---|---|---|
| 1 `BEGIN` | 2 | u8 reason: 1 the player left, 2 the session cannot go on in the sender's game, 3 the sender answers a `BEGIN` |
| 2 `FILE_START` | 7 + n | u8 file (0 to 3), u32 size (1 to 2 MiB), u8 name length n (1 to 48), the name |
| 3 `FILE_DATA` | 7 to 1156 | u8 file, u32 offset, 1 to 1150 bytes of the file |
| 4 `DONE` | 2 | u8 files sent |
| 5 `BYE` | 1 | none: the sender's last log line has gone out |

1. A game that ends a session while a peer is still connected (a desync, a stall, a lost player, Leave) sends that peer
   `BEGIN`, its desync report of this session in parts and `DONE`, without waiting for anything. The co-op log is sent
   as fast as the window takes it from here on, ahead of the files.
2. A game that receives `BEGIN` while its session runs ends it too, with a text that names the sender, and does the same.
   A client's only peer is its host. A host before the level answers a client's `BEGIN` with `DONE` and `BYE` and goes
   on with the others, and so does a host in a level while another client is there: the level then starts again for
   those who stay (Recovery). A second `BEGIN` changes nothing.
3. Once `DONE` came from every peer, a game writes `session closed`, lets the log drain and sends `BYE`. It closes the
   connection once `BYE` came from every peer and all it sent is acknowledged, a peer that went is waited for no more,
   and after `COOP_HANDOVER_MS`, 8000 ms, it closes whatever stands. It pumps SDL's events while it waits. So both sides
   do the same in either order, and the lines up to `session closed` reach the other side.
4. A received file goes into the game's own folder under its name, through `<name>.part` and a rename; no folder is
   made. The name has to be exactly what `desync_report.c` writes for the sender's own player, which the receiver takes
   from the session, never from the message: `coop-desync-<frame>-player<n>.txt` or `...-player<n>-<k>.txt`, frame
   below 2^32 without a leading zero, k 2 to 9. So no name holds a path, and none is the receiver's own.
5. At most 4 files per peer, 2 MiB each, 4 MiB from all peers together. A file with a name or size out of bounds, a part
   out of place or beyond its size, or a file not whole at `DONE` is refused; a message that cannot be read is dropped
   and counted. Neither ends anything: the session ends as it was going to.

Nothing is passed on: with three players a client gets the host's report, the host everybody's.

## Id ranges

| Range | Area |
|---|---|
| 0x0000-0x000F | Transport and handshake (hello, version, ping, disconnect) |
| 0x0010-0x003F | Session and lobby |
| 0x0040-0x007F | Sync: inputs, frame acks, state hashes, snapshots |
| 0x0080-0x00BF | Game events that are not part of the sync model (chat, pause requests) |
| 0x00C0-0x00EF | Debug and test harness |
| 0x00F0-0x00FF | Reserved |

## Registry

| Id | Name | Direction | Fields | Added in |
|---|---|---|---|---|
| 0x0000 | `CONNECT_REQUEST` | client to host | none; the connection id field carries the client's nonce | WP-20 |
| 0x0001 | `CONNECT_ACCEPT` | host to client | none | WP-20 |
| 0x0002 | `CONNECT_REFUSE` | host to client | u8 reason: 1 host full, 2 other protocol version | WP-20 |
| 0x0003 | `DISCONNECT` | both | none | WP-20 |
| 0x0004 | `ACK` | both | none; sent when there is no other datagram to carry the ack | WP-20 |
| 0x0005 | `PING` | both | u32 sender's clock in ms | WP-20 |
| 0x0006 | `PONG` | both | u32 the clock value from the ping | WP-20 |
| 0x0010 | `SESSION_HELLO` | client to host | identity | WP-21 |
| 0x0011 | `SESSION_WELCOME` | host to client | u8 player index (1 to 3), u64 session id, then the host's identity | WP-21, Recovery |
| 0x0012 | `SESSION_REFUSE` | host to client | u32 reasons: bit 0 build, 1 ROM, 2 CVars, 3 platform, 8 the hello could not be read, 9 a level has started (WP-24), 10 not the session the client was in (Recovery); 0 means no hello in time. Then the host's identity. | WP-21 |
| 0x0013 | `SESSION_PICK` | client to host | u8 character asked for (3 to 5; 2 is refused as taken) | WP-34 |
| 0x0014 | `SESSION_PICK_REFUSE` | host to client | u8 character asked for (2 to 5). The only reason is that someone else holds it. | WP-34 |
| 0x0015 | `SESSION_ROSTER` | host to client | u8 for each of the 4 players, see Characters | WP-34 |
| 0x0016 | `SESSION_LEVEL_START` | host to client | level, phase, run, roster and values, see Level start (the cleared flag of version 7 is gone) | WP-24, #191, Recovery |
| 0x0017 | `SESSION_SAVE` | host to client | the host's `Save`, 256 bytes, just ahead of every level start, see Session's save | decision 0009 item 8 |
| 0x0019 | `SESSION_REJOIN` | client to host | u64 session id, u8 character, identity: a player of the session comes back, see Recovery | Recovery |
| 0x001A | `SESSION_RECOVER` | host to client | u8 reason (1 to 3), u8 player or 0xFF: the level is going to start again, see Recovery | Recovery |
| 0x0040 | `SYNC_INPUT` | both | u8 run, then frames of each player, see Lockstep | WP-22 |
| 0x0041 | `SYNC_HASH` | both | u8 run, then the total state hash of the last 10 frames, see Desync check | WP-23 |
| 0x0042 | `SYNC_DESYNC` | both | u8 run, then the sender's hash of the first differing frame, region by region, see Desync check | WP-23 |
| 0x0043 | `SYNC_DUMP` | both | a part of the sender's dump of the desync frame, see Desync check | #189 |
| 0x0080 | `HANDOVER` | both | u8 kind (1 `BEGIN`, 2 `FILE_START`, 3 `FILE_DATA`, 4 `DONE`, 5 `BYE`), then its fields, see Handover | #189 |
| 0x00C0 | `TEST_RELIABLE` | both | test pattern, only sent by `coop_net_test` | WP-20 |
| 0x00C1 | `TEST_UNRELIABLE` | both | test pattern, only sent by `coop_net_test` | WP-20 |
| 0x00C2 | `TEST_DONE` | both | none, only sent by `coop_net_test` | WP-20 |
| 0x00C3 | `TEST_SESSION` | both | the text `session test`, only sent by `coop_session_test` | WP-21 |
| 0x00C4 | `DIAG_LOG` | both | u8 origin player (0 to 3), u8 flags (0), then lines of that player's co-op log, each ending in `\n`, see Co-op log | #172 |
