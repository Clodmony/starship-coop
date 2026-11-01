# Maintaining

For maintainers: GitHub setup, upstream syncs, releases.

## First publish (not done yet)

The local repository has full Starship history, `main` as the co-op integration branch, and a remote `upstream`
(HarbourMasters/Starship) whose push URL is disabled.

Create the GitHub repository as a **standalone repository, not a GitHub fork**. A GitHub fork defaults every new pull
request to target HarbourMasters/Starship, which is easy to get wrong with many contributors, and forks start with
Issues turned off.

```sh
gh repo create <owner>/starship-coop --public --source . --remote origin
git push -u origin main
tools/coop/setup-labels.sh <owner>/starship-coop
```

Then in the repository settings:

- Features: Issues on, Discussions on (questions and ideas that are not work items).
- Pull requests: allow squash merging (default), turn off merge commits for feature branches, delete head branches
  automatically.
- Branch protection (ruleset) on `main`: require a pull request with 1 approval, require review from code owners,
  dismiss stale approvals, require status checks `Co-op checks` and `Linux Validation`, block force pushes.
- Actions: allow GitHub Actions. Upstream's build workflows (`.github/workflows/main.yml`, `linux.yml`,
  `windows.yml`, `mac.yml`, `switch.yml`) need no secrets and run as they are.
- Add contributors as collaborators with Write access, or let them work from their own forks.
- Edit `.github/CODEOWNERS` when someone takes ownership of an area.

Finally, open one issue per work package in `docs/coop/ROADMAP.md` with the work-package form.

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
git fetch upstream
git switch -c sync/upstream-$(date +%F) origin/main
git merge --no-ff upstream/main
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
from `upstream/main`, cherry-pick, and open the PR against HarbourMasters/Starship from a personal fork. Follow their
contribution rules, and disclose AI use.

## Releases

Not yet. When M2 is reached: tag `vX.Y.Z-coop`, let CI build, attach the artifacts, and write release notes with the
QA level and known issues.
