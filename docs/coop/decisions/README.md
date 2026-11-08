# Decision records

One file per architecture decision: `NNNN-short-title.md`, numbered in order. Copy `0000-template.md`.

- **Proposed** records are open for discussion on their PR. Code may prototype against them but must not be merged
  as if they were settled.
- **Accepted** records bind everyone. To change one, write a new record that supersedes it; do not edit the old one
  beyond setting its status to `Superseded by NNNN`.
- Open a decision record as its own PR, before the code that depends on it.

| # | Title | Status |
|---|---|---|
| [0001](0001-fork-and-code-layout.md) | Fork of Starship, co-op code isolated in `src/port/coop/` | Accepted (branch layout superseded by 0003) |
| [0002](0002-sync-model.md) | Sync model: lockstep or host authority | Accepted (option A, deterministic lockstep; platform rule superseded by 0010) |
| [0003](0003-branch-layout.md) | Branch layout: `coop` integrates, `main` mirrors upstream | Accepted |
| [0004](0004-transport.md) | Transport: UDP on SDL2_net with our own reliability layer | Accepted |
| [0005](0005-players-and-characters.md) | Players and characters: player 1 is Fox, players 2-4 pick a wingman | Accepted |
| [0006](0006-human-flown-wingman.md) | What a human-flown wingman is: the character's shields follow the player | Accepted |
| [0007](0007-lives-and-respawn.md) | Lives and respawn: a shared pool, players 2-4 return while Fox is refilled in place | Accepted |
| [0008](0008-routes-and-score.md) | Routes, warps, hit count and medals: the team earns, the host steers, the team scores | Accepted |
| [0009](0009-vehicles-cutscenes-save.md) | Vehicles, cutscene timing and save | Proposed; item 6 Accepted (a vehicle per player) |
| [0010](0010-cross-platform-sessions.md) | Cross-platform sessions: a platform difference warns, it does not refuse | Accepted |
| [0011](0011-deterministic-sine-in-sessions.md) | Sine and cosine in a session come from the N64's own code, not the C library | Accepted |
