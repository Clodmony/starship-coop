# Wire protocol

Status: no messages exist yet. This page sets the rules so parallel work does not collide.

## Rules

1. **Every message has an id from the registry below**, reserved in the same PR that adds the message. Ranges per
   area keep parallel branches from picking the same number; take the next free id in your area's range.
2. **Versioned handshake.** The first exchange carries a protocol version, the build id (git commit), the ROM version
   (US 1.0 or US 1.1) and a hash of the simulation-affecting CVars. Any mismatch refuses the connection with a
   message the player can read. Bump `COOP_PROTOCOL_VERSION` whenever a message layout changes.
3. **Explicit layout.** Little-endian, fixed-size integer types, no raw `memcpy` of game structs (they hold pointers
   and padding). Write a serializer per message.
4. **Frame-stamped.** Messages that belong to a game frame carry that frame number.
5. **No game data on the wire** beyond what the protocol needs (no asset or ROM bytes).
6. Document each message here: id, name, direction, fields, when it is sent.

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
| | | | | |
