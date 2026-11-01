# Maintaining

For maintainers: GitHub setup, upstream syncs, releases.

## GitHub setup

`Clodmony/starship-coop` is a **standalone repository, not a GitHub fork** of HarbourMasters/Starship: a GitHub fork
would default every new pull request to target HarbourMasters, which is easy to get wrong with many contributors.
Remote names are fixed in CONTRIBUTING.md ("Remotes"): `origin` is this repo, `starship` is HarbourMasters.

The repository is **private** for now. Applied (re-apply if it is ever recreated):

- Features: Issues and Discussions on; Wiki and Projects off.
- Merging: squash (default for work branches) and merge commits (for upstream syncs) allowed, rebase merging off,
  head branches deleted after merge.
- Labels: `tools/coop/setup-labels.sh Clodmony/starship-coop`.
- Milestones `M0` to `M3` and one issue per work package, linked from `docs/coop/ROADMAP.md`.

**Not applied yet: the branch ruleset.** GitHub only allows rulesets on private repositories with a paid plan, so
`main` is unprotected until the repository is public (or the account has Pro). The ruleset is stored in
`.github/rulesets/main.json`: no deletion, no force push, changes only through pull requests with 1 approval from a
code owner and stale approvals dismissed, required status check `Co-op checks`. Repository admins may bypass it for
pull requests only, so a sole maintainer can merge their own PRs; direct pushes stay blocked. Apply it with:

```sh
gh repo edit Clodmony/starship-coop --visibility public --accept-visibility-change-consequences
gh api -X POST repos/Clodmony/starship-coop/rulesets --input .github/rulesets/main.json
```

**Actions minutes while private:** private repositories have a monthly quota of free Actions minutes, and macOS
runners count ten times. Upstream's `GenerateBuilds` builds four platforms on every push to any branch, and the
four validation workflows (Linux, Windows, macOS, Switch) build on every PR. Public repositories have no quota for
standard runners.

Upstream's build workflows (`.github/workflows/main.yml`, `linux.yml`, `windows.yml`, `mac.yml`, `switch.yml`) need
no secrets. `main.yml` builds every platform on every push to any branch; the validation workflows build on every
PR. Their jobs are all named `build`, so they cannot be told apart as required checks; reviewers check them by hand.
Making them required would need a change to upstream files, which we avoid.

When more people help maintain: add them as collaborators (Write for contributors, Maintain or Admin for
maintainers) and give them areas in `.github/CODEOWNERS`.

## Labels

`tools/coop/setup-labels.sh` creates them. Meaning:

| Label | Use |
|---|---|
| `type/work-package` | A roadmap item |
| `type/bug`, `type/desync` | Something broken; desync = clients disagree |
| `type/research` | Find out how something works; result goes to NOTES.md |
| `type/decision` | Needs a decision record |
| `area/*` | `engine`, `render`, `input`, `ui`, `net`, `sync`, `test`, `build`, `docs` |
| `status/claimed`, `status/blocked`, `status/needs-review` | Where it stands |
| `upstream` | Should also go to HarbourMasters/Starship |
| `good first issue`, `help wanted` | For newcomers |

## Upstream sync

Do it regularly (at least after every upstream release) so the gap stays small. The `starship-upstream-sync` skill
has the same steps for agents.

```sh
git fetch --multiple origin starship
git switch -c sync/upstream-$(date +%F) origin/main
git merge --no-ff starship/main
git submodule update --init --recursive
```

- Conflicts in files marked `// COOP:` are expected: keep both intents, re-apply the co-op change to the new upstream
  code. `git grep -n "COOP:"` before and after must list the same places (plus any new ones you had to add).
- `README.md`: keep ours. Upstream's README is linked from it.
- `.gitignore`: keep both.
- Build, run the single-player replay checks from `docs/coop/QA.md` once they exist, then open the PR.
- Merge sync PRs with a **merge commit**, not squash, so upstream history stays intact and the next sync is clean.

## Sending fixes upstream

Generic fixes (port bugs, build fixes, new hook events that are not co-op specific) belong upstream as well. Branch
from `starship/main`, cherry-pick, and open the PR against HarbourMasters/Starship from a personal fork of
HarbourMasters/Starship. Follow their contribution rules, and disclose AI use.

## Releases

Not yet. When M2 is reached: tag `vX.Y.Z-coop`, let CI build, attach the artifacts, and write release notes with the
QA level and known issues.
