# Maintaining

For maintainers: GitHub setup, upstream syncs, releases.

## GitHub setup

`Clodmony/starship-coop` is a **standalone repository, not a GitHub fork** of HarbourMasters/Starship: a GitHub fork
would default every new pull request to target HarbourMasters, which is easy to get wrong with many contributors.
Remote names are fixed in CONTRIBUTING.md ("Remotes"): `origin` is this repo, `starship` is HarbourMasters.

The repository is **public**. Applied (re-apply if it is ever recreated):

- Features: Issues and Discussions on; Wiki and Projects off.
- Merging: squash (default for work branches) and merge commits (for upstream syncs) allowed, rebase merging off,
  head branches deleted after merge.
- Labels: `python tools/coop/setup_labels.py Clodmony/starship-coop`.
- Milestones `M0` to `M3` plus `Later`, and one issue per work package, linked from `docs/coop/ROADMAP.md`.
- Branch ruleset `main` (applied 2026-11-01), stored in `.github/rulesets/main.json`: no deletion, no force push,
  changes only through pull requests with 1 approval from a code owner and stale approvals dismissed, required
  status check `Co-op checks`. Repository admins may bypass it for pull requests only, so a sole maintainer can
  merge their own PRs; direct pushes stay blocked. To recreate it:

```sh
gh api -X POST repos/Clodmony/starship-coop/rulesets --input .github/rulesets/main.json
```

GitHub only allows rulesets on private repositories with a paid plan; if the repository ever goes private again,
`main` loses this protection.

## CI

Local builds are the development loop (`python tools/coop/build.py`); CI is the final check on pull requests.

| Workflow | File | Runs | Jobs |
|---|---|---|---|
| `Co-op checks` | `coop-checks.yml` | every PR, every push to `main` | repo checks, seconds |
| `Co-op build` | `coop-build.yml` | PRs and pushes to `main` that touch code, CMake files or submodules; manual | Linux and Windows, both through `tools/coop/build.py` |

Upstream's workflows (`GenerateBuilds` in `main.yml`, and the `Linux`, `Windows`, `MacOS`, `Switch` validation
workflows) are **disabled in the repository settings**, not deleted, so their files stay identical to upstream and
upstream syncs do not conflict. `GenerateBuilds` built four platforms on every push to any branch, which is too much
for a project with many contributors. Re-enable one for a release build with `gh workflow enable <name>`; release
builds are WP-30. As of 2026-11-01 upstream's macOS jobs target the `macos-14` runner image, which GitHub retires on
2026-11-02, so they need an upstream fix (or our own release workflow) before macOS builds work again.

```sh
for w in GenerateBuilds "Linux Validation" "Windows Validation" "MacOS Validation" "Switch Validation"; do
  gh workflow disable "$w" --repo Clodmony/starship-coop
done
```

**Actions minutes while private:** private repositories have a monthly quota of free Actions minutes; Windows
minutes count twice and macOS minutes ten times. A code PR costs about 40 Linux minutes plus 35 Windows minutes
(counted as 70). Public repositories have no quota for standard runners.

When more people help maintain: add them as collaborators (Write for contributors, Maintain or Admin for
maintainers) and give them areas in `.github/CODEOWNERS`.

## Labels

`python tools/coop/setup_labels.py` creates them. Meaning:

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
