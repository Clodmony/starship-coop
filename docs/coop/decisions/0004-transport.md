# 0004: Transport: UDP on SDL2_net with our own reliability layer

- Status: Accepted
- Date: 2026-11-04
- Issue: #18 (WP-20)

## Context

DESIGN.md section 3 puts the transport behind one interface: "SDL2_net first, Steam relay later". Nothing settles what
runs over SDL2_net, which offers both TCP and UDP. The answer has to hold for either outcome of decision 0002, which
is still open.

Observed:

- `CMakeLists.txt` already has a `USE_NETWORKING` switch that links SDL2_net, and the Linux CI job already installs
  `libsdl2-net-dev` (`.github/workflows/coop-build.yml`).
- SDL2_net addresses are IPv4 only (`IPaddress` in `SDL_net.h` holds a 32-bit host).
- The Steam relay planned for WP-26 sends whole messages, reliable or unreliable
  ([ISteamNetworkingMessages](https://partner.steamgames.com/doc/api/ISteamNetworkingMessages)).
- The chaos relay that QA.md plans for loss and lag tests is UDP only
  ([README](https://github.com/cranebeaker/coop-anything/blob/main/tools/chaos/README.md)).

Derived:

- Lockstep (0002 option A) waits for every player's input each frame, so one late message stalls the game for
  everyone. That favours a transport where a lost packet does not hold back the packets behind it.

Unknown:

- Which sync model wins, and so whether any message needs more than one datagram.

## Options

### A. UDP on SDL2_net, with our own reliability layer

Datagrams carry whole messages. We add sequence numbers, acks, resends, keep-alive and a checksum.

- Fits the plan as written: no new dependency, and SDL2_net is already wired into the build.
- The same message shape as the Steam relay, so one interface covers both.
- Has an unreliable lane, so a late input does not block newer ones.
- The chaos relay can test it directly.
- Costs a few hundred lines of our own code. Reliability bugs are subtle, so it needs testing under loss.
- No fragmentation unless we write it.

### B. TCP on SDL2_net

- Least code: the OS handles loss and ordering, and we only add message framing.
- One lost packet holds back everything behind it until the resend arrives. There is no unreliable lane.
- The chaos relay cannot touch it, so loss tests need another tool.
- A stream does not match the Steam relay's messages, so the interface would have to hide that difference.

### C. ENet

A proven C library (MIT) and the default in the hub's
[networking page](https://github.com/cranebeaker/coop-anything/blob/main/playbook/networking.md).

- Reliable and unreliable lanes and fragmentation with little code of our own.
- A new dependency on all three desktop platforms, on top of or in place of the SDL2_net hook upstream already has.
- Changes the plan in DESIGN.md and in WP-20.

## Decision

Option A. The transport is message based. A peer sends whole messages, each with an id from
`docs/coop/PROTOCOL.md`, on one of two lanes: **reliable** (ordered, delivered exactly once) or **unreliable** (may be
lost or arrive out of order, never delivered twice). The host accepts peers, clients connect to one host. The
reliability code stays separate from the sockets, so it can run in a test without a network.

## Consequences

- WP-26 implements the same interface on Steam's own reliability, without our layer.
- One message fits one datagram: about 1200 bytes, and larger ones are refused. If decision 0002 needs snapshots,
  fragmentation is new work.
- IPv4 only, no encryption, no authentication of peers. Every received byte is untrusted and must be bounds checked.
- The chaos relay moves into `tools/coop/chaos/` (MIT, from the hub), so anyone can repeat the tests, and WP-27 can
  build on it.
