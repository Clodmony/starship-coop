# Wire protocol

Status: the transport messages exist (WP-20). Session, sync and game messages come with their work packages. This
page sets the rules so parallel work does not collide.

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
| 0x00C0 | `TEST_RELIABLE` | both | test pattern, only sent by `coop_net_test` | WP-20 |
| 0x00C1 | `TEST_UNRELIABLE` | both | test pattern, only sent by `coop_net_test` | WP-20 |
| 0x00C2 | `TEST_DONE` | both | none, only sent by `coop_net_test` | WP-20 |
