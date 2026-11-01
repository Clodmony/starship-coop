# Contributing to Starship Co-op

Humans and agents follow the same flow. Agents also read [AGENTS.md](AGENTS.md).

## Get the code

You need your own US 1.0 or US 1.1 Star Fox 64 ROM to run the game (see [docs/BUILDING.md](docs/BUILDING.md)).

```sh
git clone --recursive https://github.com/Clodmony/starship-coop.git
cd starship-coop
```

Remotes, the same for everyone so docs and scripts work everywhere:

| Remote | Points at | Who needs it |
|---|---|---|
| `origin` | `Clodmony/starship-coop`, the co-op repo. `origin/main` is what you branch from. | Everyone |
| `fork` | Your own fork of starship-coop, to push branches when you have no write access | Contributors without write access |
| `starship` | `HarbourMasters/Starship`, the upstream port. Never push to it. | Whoever syncs upstream |

```sh
# No write access: fork on GitHub without touching your remotes, then add it as "fork".
gh repo fork Clodmony/starship-coop --remote=false
git remote add fork https://github.com/<you>/starship-coop.git
git push -u fork coop/12-p2-input-routing
gh pr create --repo Clodmony/starship-coop --head <you>:coop/12-p2-input-routing --draft

# Upstream syncs only:
git remote add starship https://github.com/HarbourMasters/Starship.git
git remote set-url --push starship DISABLED
```

Don't let `gh repo fork --remote` rename your remotes; it would call this repo `upstream` and break the convention.

## Find something to work on

- Work packages are listed in [docs/coop/ROADMAP.md](docs/coop/ROADMAP.md); each one has a GitHub issue with the
  `type/work-package` label. Issues labelled `good first issue` need little context.
- Bugs and desyncs are issues too. Use the issue forms; they ask for what a fix needs.
- Have an idea that is not on the roadmap? Open an issue first so nobody builds the same thing twice.

## Claim it

1. Comment on the issue that you are taking it (or assign yourself if you can). The `status/claimed` label is then
   set by a maintainer.
2. Hold one claim at a time unless a maintainer agrees otherwise.
3. A claim with no visible progress (commit, PR or comment) for 14 days can be released by a maintainer. A hand-off
   comment keeps it alive.

## Work on it

1. Branch from the latest `main`: `coop/<issue number>-<short-slug>`, for example `coop/12-p2-input-routing`.
   Upstream syncs use `sync/upstream-YYYY-MM-DD`. Docs-only changes can use `docs/<slug>`.
2. Recommended: one git worktree per issue, so several branches (and several agents) can build side by side:
   `tools/coop/new-worktree.sh 12 p2-input-routing`.
3. Follow the code rules in [AGENTS.md](AGENTS.md#code-rules): co-op code in `src/port/coop/`, upstream files
   touched only where needed and marked `// COOP:`, everything gated on an active co-op session, message ids from
   [PROTOCOL.md](docs/coop/PROTOCOL.md).
4. Write down what you learn about the game as you go, in [docs/coop/NOTES.md](docs/coop/NOTES.md), with file and
   line references. Notes written at the end are notes lost.
5. A change to the architecture (sync model, transport, player model, save handling) needs a decision record in
   [docs/coop/decisions/](docs/coop/decisions/). Open it as its own PR so it gets discussed before code depends on
   it.

## Commits

- Small and focused. Message format: `<area>: <what changed>`, for example `coop/net: add message framing`,
  `engine: COOP hook for player 2 spawn`, `docs: notes on on-rails camera`.
- Format C and C++ with the repo's `.clang-format` (`clang-format -i <files>`). Do not reformat code you did not
  change; it makes upstream merges painful.
- Run `tools/coop/check.sh` before pushing. CI runs it too.

## Pull requests

1. Open the PR as a **draft** early, with the template filled in and `Closes #<issue>`.
2. Mark it ready when the checklist is done. It needs green `Co-op checks` and one approving review from a code
   owner (`.github/CODEOWNERS`). Upstream's Linux, Windows and macOS build jobs also run on every PR; a PR that
   breaks them is not merged. Maintainers can merge their own PRs without a second review while the project has a
   single maintainer; direct pushes to `main` are blocked for everyone.
3. Say which QA level the change reached and how it was tested ([docs/coop/QA.md](docs/coop/QA.md)). "Compiles" is
   a valid answer; claiming more than you ran is not.
4. If an AI agent wrote the change, say so, and have the human who runs the agent review the diff before marking the
   PR ready.
5. One issue per PR. Unrelated fixes you noticed go in their own issue.

## Stopping halfway

Push the branch, update the draft PR, and leave a hand-off comment on the issue: done, verified (how), left to do,
the next concrete step, surprises. Then release the claim or say when you will be back.

## Please don't

- Commit ROMs, `.o2r`/`.otr` archives, extracted assets or anything else made from game data, or link to them.
- Paste personal info: real names, emails, tokens, local paths with your username.
- Reformat or reorganize upstream files. Upstream merges are everyone's problem.
- Claim a QA level you did not reach.

## License

By contributing you agree that your contribution is released under CC0 1.0, the license of this repository and of
upstream Starship ([LICENSE.md](LICENSE.md)). This keeps co-op work free to flow back upstream.
