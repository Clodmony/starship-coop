# Decision records

One file per architecture decision: `NNNN-short-title.md`, numbered in order. Copy `0000-template.md`.

- **Proposed** records are open for discussion on their PR. Code may prototype against them but must not be merged
  as if they were settled.
- **Accepted** records bind everyone. To change one, write a new record that supersedes it; do not edit the old one
  beyond setting its status to `Superseded by NNNN`.
- Open a decision record as its own PR, before the code that depends on it.

| # | Title | Status |
|---|---|---|
| [0001](0001-fork-and-code-layout.md) | Fork of Starship, co-op code isolated in `src/port/coop/` | Accepted |
| [0002](0002-sync-model.md) | Sync model: lockstep or host authority | Proposed |
