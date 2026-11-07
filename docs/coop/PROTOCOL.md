# Wire protocol

Status: the transport (WP-20), the session handshake (WP-21), the characters (WP-34), the level start (WP-24),
lockstep (WP-22) and the desync check (WP-23) exist; `COOP_PROTOCOL_VERSION` is 6. Other sync and game messages come
with their work packages. This page sets the rules so parallel work does not collide.

## Rules

1. **Every message has an id from the registry below**, reserved in the same PR that adds the message. Ranges per
   area keep parallel branches from picking the same number; take the next free id in your area's range.
2. **Versioned handshake.** The first exchange carries a protocol version, the build id (git commit), the ROM version
   (the game version of each loaded archive), the values of the simulation-affecting CVars and the platform (OS,
   CPU, compiler with its version). A different protocol version, build, ROM, CVar or platform refuses the connection
   with a message the player can read. The platform counts because lockstep needs every client to compute the same game
   and no run has compared two platforms ([decision 0002](decisions/0002-sync-model.md)); a comparison that shows two
   platforms compute the same game lifts that. For a real cross-platform test, `SF64_COOP_TEST_ANY_PLATFORM` on the host
   lets in a client that differs in platform and nothing else, and both players read what differs. Bump
   `COOP_PROTOCOL_VERSION` whenever a message layout changes.
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
- **Not provided:** lobbies and invites. The client needs the host's SteamID.

## Session handshake

The code is in `src/port/coop/session/`. All three messages go on the reliable lane.

1. When the transport reports the connection, the client sends `SESSION_HELLO` with its identity.
2. The host compares it with its own. On a match it answers `SESSION_WELCOME`, otherwise `SESSION_REFUSE`. Both
   carry the host's identity, so the client builds the same text the host shows: what differs, host value against
   joining player value.
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
4. A client that leaves, for whatever reason, frees its slot and its character at once. Nothing is kept for it: if it
   comes back it is a new client, which may find the character taken.
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

1. The host's game asks for a level with what it holds now. The host turns that down while a client that joined holds
   value 1, or while a client is in the handshake, which ends within 10000 ms. Nothing changes then, and it can ask
   again. So a client never flies with nothing: nobody starts until it has picked or left.
2. When the host takes it, the level, the phase, the values and the roster of that moment are fixed. A client that
   leaves afterwards does not change what the others start from, so every game comes to the same player count.
3. From then on nobody picks and nobody joins, and nothing opens that again yet. A `SESSION_PICK` on its way is dropped
   and counted; the client learns what it holds from the level start. A hello gets `SESSION_REFUSE` with bit 9, whatever
   its identity.
4. The host sends the message to every client that joined, on the next poll, and tries again on the next one while the
   window is full. Its own game hears of it on that poll too. Each game then enters the level from the message, the
   host's as well, so no game goes by its own history.

| Size | Field |
|---|---|
| 1 | Level: a `LevelId` below 20, so not versus |
| 1 | Phase: `gLevelPhase` |
| 4 | Roster, as in Characters |
| 4 v | The values, each a little-endian 32-bit integer, in the order of the table in `level_start.c`: `COOP_LEVEL_START_VALUES`, 87 now |

Each game goes to the level by itself, as soon as it hears, and what holds them together from there on is the first
frame of lockstep, below.

What does not fit, as before:

- A second level start, one before the welcome, and one from a client are dropped and counted (`CoopSession_Ignored`).
- A client ends with `BAD_MESSAGE` on a level start of another size, with a level of 20 or more, with a roster that
  `SESSION_ROSTER` could not hold, or with value 1 anywhere in the roster.

## Lockstep

[Decision 0002](decisions/0002-sync-model.md): every client runs the whole game, and only inputs travel. The code is
`src/port/coop/sync/lockstep.c`, and `lockstep_game.c` connects it to the game. A session of one player has no lockstep.

**Frames.** Frame 0 is the first frame in `PLAY_UPDATE` after a level start, on every client. From then on every game
frame is a lockstep frame, on the map too, until the session ends. Frame F runs on a client once it holds every
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

**End.** Every frame needs every player, so a session ends for everyone when one player goes. The level goes on in each
game, with the local player on its controller and the others idle: that is the choice `Leave` already had (#126).

- A client that leaves or times out: the host ends the session, and says who left.
- The host that leaves or times out: the client sees the session end.
- `COOP_LOCKSTEP_STALL_MS`, 10000 ms, with no frame and no input from a player, on a connection that stays up: whoever
  notices ends the session and names that player. Before frame 0 it is `COOP_LOCKSTEP_START_MS`, 60000 ms, as a slow PC
  may still be loading.
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
takes the frame and the pair from it, as a third player does. A game in the pair ends the session once the other side's
hash has come, or `COOP_DESYNC_GRACE_MS`, 1000 ms, after its own went out; a game outside the pair waits the grace out,
or the host would see it leave before the pair had exchanged. Ending runs through the same path as a lost player: the
level goes on in each game with the local player on its controller and the others idle, and the co-op window comes up
with the text. The text names the frame, the level and its mode, the two players, the regions, and the report's path.

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

**What the session does after a desync: it ends.** That is the smallest behaviour that is honest: until host correction
(option C of decision 0002) exists there is no repair, and playing on would have each player in a different game that
looks like one. It is a choice for the owner to reverse; the alternatives are to play on with a warning, or C.

What does not fit:

- `SYNC_HASH` of the wrong size, with a count of 0 or above 32, on a link nobody is on; `SYNC_DESYNC` of the wrong size,
  from a client for anyone but itself, with a pair of one player or of a player who is not in the level: the receiver
  ends the session.
- Before this game has reached frame 0, both are dropped.

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
| 0x0011 | `SESSION_WELCOME` | host to client | u8 player index (1 to 3), then the host's identity | WP-21 |
| 0x0012 | `SESSION_REFUSE` | host to client | u32 reasons: bit 0 build, 1 ROM, 2 CVars, 3 platform, 8 the hello could not be read, 9 a level has started (WP-24); 0 means no hello in time. Then the host's identity. | WP-21 |
| 0x0013 | `SESSION_PICK` | client to host | u8 character asked for (3 to 5; 2 is refused as taken) | WP-34 |
| 0x0014 | `SESSION_PICK_REFUSE` | host to client | u8 character asked for (2 to 5). The only reason is that someone else holds it. | WP-34 |
| 0x0015 | `SESSION_ROSTER` | host to client | u8 for each of the 4 players, see Characters | WP-34 |
| 0x0016 | `SESSION_LEVEL_START` | host to client | level, phase, roster and values, see Level start | WP-24 |
| 0x0040 | `SYNC_INPUT` | both | frames of each player, see Lockstep | WP-22 |
| 0x0041 | `SYNC_HASH` | both | the total state hash of the last 10 frames, see Desync check | WP-23 |
| 0x0042 | `SYNC_DESYNC` | both | the sender's hash of the first differing frame, region by region, see Desync check | WP-23 |
| 0x00C0 | `TEST_RELIABLE` | both | test pattern, only sent by `coop_net_test` | WP-20 |
| 0x00C1 | `TEST_UNRELIABLE` | both | test pattern, only sent by `coop_net_test` | WP-20 |
| 0x00C2 | `TEST_DONE` | both | none, only sent by `coop_net_test` | WP-20 |
| 0x00C3 | `TEST_SESSION` | both | the text `session test`, only sent by `coop_session_test` | WP-21 |
