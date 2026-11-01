# 0001: Fork of Starship, co-op code isolated in `src/port/coop/`

- Status: Accepted; the branch layout ("the fork's own `main` is the integration branch") is superseded by 0003
- Date: 2026-11-01
- Issue: (none, project setup)

## Context

Co-op for a decomp-based port is ordinary game programming in the port's code (Co-op Anything,
[decomp and recomp, section 4b](https://github.com/cranebeaker/coop-anything/blob/main/playbook/decomp-and-recomp.md)).
Upstream Starship keeps moving (libultraship bumps, enhancements, fixes), and a fork that drifts stops getting them.
Many contributors work in parallel, so where code goes must be predictable.

## Options

1. A fork of the whole Starship repository with co-op code inside it.
2. A separate repository with Starship as a submodule and co-op as patch files.
3. A Starship mod loaded at runtime.

Option 2 makes every change a patch-on-patch and is hard to build and debug. Option 3 is not possible today: the port
has no code-mod loader for the kind of engine changes co-op needs (more than one player in the campaign).

## Decision

A full fork (option 1), with full upstream history; HarbourMasters/Starship is the git remote `starship`. Co-op code
lives in `src/port/coop/`; upstream files are changed only where needed, each change marked `// COOP:`; port event
hooks are preferred over direct edits. The fork's own `main` is the integration branch.

## Consequences

- `git grep -n "COOP:"` lists every divergence from upstream game code, which keeps upstream merges reviewable.
- Generic fixes (not co-op specific) should go upstream too, so the diff stays small.
- The repository license stays CC0 1.0 like upstream.
