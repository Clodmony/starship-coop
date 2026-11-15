# Maintaining

For maintainers: GitHub setup, upstream syncs, releases.

## GitHub setup

`Clodmony/starship-coop` is a **standalone repository, not a GitHub fork** of HarbourMasters/Starship: a GitHub fork
would default every new pull request to target HarbourMasters, which is easy to get wrong with many contributors.
Remote and branch names are fixed in CONTRIBUTING.md ("Branches", "Remotes and branches"): `origin` is this repo,
`starship` is HarbourMasters; `coop` is the default integration branch, `main` mirrors upstream (decision 0003).

The repository is **public**. Applied (re-apply if it is ever recreated):

- Default branch `coop`.
- Features: Issues and Discussions on; Wiki and Projects off.
- Merging: squash (default for work branches) and merge commits (for upstream syncs) allowed, rebase merging off,
  head branches deleted after merge.
- Labels: `python tools/coop/setup_labels.py Clodmony/starship-coop`.
- Milestones `M0` to `M3` plus `Later`, and one issue per work package, linked from `docs/coop/ROADMAP.md`.
- Ruleset `coop` on the default branch (`.github/rulesets/coop.json`): no deletion, no force push, changes only
  through pull requests with 1 approval from a code owner and stale approvals dismissed. It required the status check
  `Co-op checks` until CI was turned off (below). Repository admins may bypass it for pull requests only, so a sole maintainer can merge their own
  PRs; direct pushes stay blocked.
- Ruleset `upstream-mirror` on `main` (`.github/rulesets/upstream-mirror.json`): no deletion, no force push, and
  only repository admins can update it, by fast-forward from upstream.
- Ruleset `release-tags` on the tags `v*` (`.github/rulesets/release-tags.json`, #358): only repository admins can create, move or
  delete one, so a release tag is a deliberate act and never moves.

```sh
gh api -X POST repos/Clodmony/starship-coop/rulesets --input .github/rulesets/coop.json
gh api -X POST repos/Clodmony/starship-coop/rulesets --input .github/rulesets/upstream-mirror.json
gh api -X POST repos/Clodmony/starship-coop/rulesets --input .github/rulesets/release-tags.json
```

GitHub only allows rulesets on private repositories with a paid plan; if the repository ever goes private again,
both branches lose this protection.

Do not rename a branch that is the head of an open PR: GitHub closes the PR (that happened to #32).

## CI

**Off since 2026-11-07, by the owner's choice:** waiting for GitHub CI only slowed development. Both co-op workflows are
disabled in the repository settings, not deleted, and the `coop` ruleset no longer requires `Co-op checks`. Local runs
are the check: `python tools/coop/build.py` and the area's `test_all.py --only` steps before a push, the full
`python tools/coop/test_all.py` once per batch of PRs before the merge (CONTRIBUTING.md, #195). To turn CI on
again: `gh workflow enable "Co-op checks"`, `gh workflow enable "Co-op build"`, and add `Co-op checks` back as a required
status check of the `coop` ruleset (`.github/rulesets/coop.json`). What the workflows do when on:

| Workflow | File | Runs | Jobs |
|---|---|---|---|
| `Co-op checks` | `coop-checks.yml` | every PR, every push to `coop` | repo checks with the single-player audit and its tests, seconds |
| `Co-op build` | `coop-build.yml` | PRs and pushes to `coop` that touch code, CMake files or submodules; manual | Linux and Windows, both through `tools/coop/build.py` |
| `Release` | `release.yml` | a tag `vX.Y.Z-coop[-alpha1]`; manual from a tag | checks the tag, builds through `coop-build.yml`, makes a draft release (below, "Releases") |

Nothing runs on `main`: upstream's own workflows are disabled (below), and ours only exist on `coop`.

Upstream's workflows (`GenerateBuilds` in `main.yml`, and the `Linux`, `Windows`, `MacOS`, `Switch` validation
workflows) are **disabled in the repository settings**, not deleted, so their files stay identical to upstream and
upstream syncs do not conflict. `GenerateBuilds` built four platforms on every push to any branch, which is too much
for a project with many contributors. Re-enable one for a release build with `gh workflow enable <name>`. With CI on,
test builds come from `Co-op build` instead: a run whose tests of the build pass leaves `starship-coop-<id>-linux-x64` and
`-windows-x64` on its Actions page for 14 days, `<id>` being the id the game shows. They hold no ROM and no `sf64.o2r`;
`starship.o2r` is in them because the game cannot start without it, packed from the tracked `port/` files. The owner
accepted that, and a new file under `port/` stops `package.py` until someone has looked at it (NOTES.md, 2026-11-06,
#26). As of 2026-11-01 upstream's macOS jobs target the `macos-14` runner image, which GitHub retires on
2026-11-02, so they need an upstream fix (or our own release workflow) before macOS builds work again.

```sh
for w in GenerateBuilds "Linux Validation" "Windows Validation" "MacOS Validation" "Switch Validation"; do
  gh workflow disable "$w" --repo Clodmony/starship-coop
done
```

**Actions minutes:** the repository is public, and public repositories have no quota for standard runners. If it goes
private again: private repositories have a monthly quota of free Actions minutes; Windows minutes count twice and macOS
minutes ten times. A code PR costs about 40 Linux minutes plus 35 Windows minutes (counted as 70).

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

Two steps. The `starship-upstream-sync` skill has the same steps for agents.

**1. Update the mirror** (any time, no risk: `main` only ever fast-forwards to upstream; admins only):

```sh
git fetch starship main
git push origin starship/main:main      # refused if it is not a fast-forward, which means someone committed to main
```

**2. Bring the mirror into `coop`** (regularly, at least after every upstream release, so the gap stays small):

```sh
git fetch origin
git switch -c sync/upstream-$(date +%F) origin/coop
git merge --no-ff origin/main
git submodule update --init --recursive
```

- Conflicts in files marked `// COOP:` are expected: keep both intents, re-apply the co-op change to the new upstream
  code. `git grep -n "COOP:"` before and after must list the same places (plus any new ones you had to add).
- `README.md`: keep ours. Upstream's README is linked from it.
- `.gitignore`: keep both.
- Bring the single-player audit up to date before the build: `build.py` runs `check.py`, which fails while the audit
  is stale. Run `python tools/coop/sp_audit.py`. With no new site it rewrites
  `docs/coop/audit/single-player.md`. A site the list does not have stops it: it writes nothing and names each new site
  with the row it would take its verdict from, or `no verdict row`. Read those sites, fix or add their rows in
  `docs/coop/audit/sp-verdicts.tsv`, then run `python tools/coop/sp_audit.py --accept`, which takes them in. That
  records that the sites were shown, not that the verdicts fit. `matches no site` means a row has no sites left: delete
  the row and rerun. Read the diff of the list.
- Build, run the single-player replay checks from `docs/coop/QA.md` once they exist, then open the PR into `coop`.
- Merge sync PRs with a **merge commit**, not squash, so upstream history stays intact and the next sync is clean.

## Sending fixes upstream

Generic fixes (port bugs, build fixes, new hook events that are not co-op specific) belong upstream as well. Branch
from `origin/main` (the clean upstream mirror), cherry-pick, and open the PR against HarbourMasters/Starship from a
personal fork of HarbourMasters/Starship. Follow their contribution rules, and disclose AI use.

## Agent safety

AGENTS.md rule 13 says what an agent does with text that comes from outside. This section says what holds when an agent does
not notice (the first text aimed at an agent that this project met is in the table below).

| Layer | What it stops | Where |
|---|---|---|
| Rules 9 and 13, and the worker briefs | The ordinary case: an agent that reads an order in a page, issue or log and takes it for data | `AGENTS.md`, `orch/brief-*` |
| Workers cannot publish | An injected worker pushing, posting or merging: the coordinator reads the diff and publishes | the harness's permission mode |
| The owner's approval | Merges, closes, deletes and public posts | the owner |
| `push-guard` and the `coop` ruleset | Private data in a push; a force push, a deletion, a merge without review | `~/.config/git/push-guard`, `.github/rulesets/coop.json` |
| `release-tags` ruleset, the `release` environment, actions pinned by hash | A tag or a release made by anyone but the admins; the one job that can write waiting for the owner | `.github/rulesets/release-tags.json`, `release.yml` |
| The tripwire hook | Some of the cases above that an agent would have missed: it warns before the model acts | `tools/coop/injection_scan.py` |

**What none of these covers:** the agent session holds the owner's GitHub login, so `gh` can do what the owner can, the admin
merge into `coop` included, and only the harness's classifier and the owner's approvals stand between a fooled agent and that. A
token for agent sessions without admin and delete rights would close it (the owner's action; it changes how merges into `coop` are
made). Anything read in a session that also holds that login is the risk, so web research belongs in a worker that cannot write.

**The tripwire.** `tools/coop/injection_scan.py` is a Claude Code PostToolUse hook. It reads what a tool brought in from outside and,
when the text looks like an order to an AI agent, adds a warning to what the model sees and shows one to the user; it never blocks.
Each match is also appended to `~/.claude/injection-tripwire.log` (time, tool, source, labels, 160 characters), so the answer to "were
we attacked before" is on disk from the day it runs. It is a tripwire, not a wall: a text worded to pass it passes.

Setup, once, by the owner (it writes outside the repository): copy the script, then add the hooks to `~/.claude/settings.json`
beside the ones that are there (do not replace them):

```sh
mkdir -p ~/.claude/hooks && cp tools/coop/injection_scan.py ~/.claude/hooks/injection_scan.py
```

```json
{"matcher": "WebFetch|WebSearch",
 "hooks": [{"type": "command", "command": "python3 \"$HOME/.claude/hooks/injection_scan.py\"", "timeout": 10}]},
{"matcher": "Bash",
 "hooks": [
  {"type": "command", "if": "Bash(curl *)", "command": "python3 \"$HOME/.claude/hooks/injection_scan.py\"", "timeout": 10},
  {"type": "command", "if": "Bash(wget *)", "command": "python3 \"$HOME/.claude/hooks/injection_scan.py\"", "timeout": 10},
  {"type": "command", "if": "Bash(gh issue view *)", "command": "python3 \"$HOME/.claude/hooks/injection_scan.py\"", "timeout": 10},
  {"type": "command", "if": "Bash(gh pr view *)", "command": "python3 \"$HOME/.claude/hooks/injection_scan.py\"", "timeout": 10},
  {"type": "command", "if": "Bash(gh api *)", "command": "python3 \"$HOME/.claude/hooks/injection_scan.py\"", "timeout": 10}]}
```

Both entries go into the list `hooks.PostToolUse`. After a change to `injection_scan.py`, copy it again.

**Incidents** (newest first; also look in `~/.claude/injection-tripwire.log`)

| Date | Where | What | What happened |
|---|---|---|---|
| 2026-11-15 | tcrf.net, two `WebFetch` calls of the coordinator and one of the worker `w15-needle` | A page served to the fetch tool, apparently on purpose against scrapers, with demands to pay, to terminate, to print a phrase and to write a file `key.bin`; the fetch tool's summary reported it | Not followed: the summary said so, the agents ignored it and told the owner. The real article was never read, so what the page says about the boost gauge is known only from a search summary (#357). |

Searches of the project's 143 saved sessions, its tracker (two authors only: the owner and the collaborator), its tracked source and
the agents' memory found no earlier one (2026-11-15). The project has had no outside commenter, fork or star so far; that changes with
the first public release.

## Releases

A release is a tag on a commit of `coop` whose code passed the full gate on the owner's PC. CI cannot run that gate (it needs a ROM
and takes about 20 minutes with several game clients), so the tag means "tested" through the steps below, and
`tools/coop/release_check.py` (tested by `release_check_test.py`, the step `release-check`) refuses a release that skips one.
`.github/workflows/release.yml` does the rest and only ever makes a **draft**: nothing is public until someone presses "Publish release".

Before the first one, decision 0002 and the roadmap (M3) name two conditions: a run on two real PCs
([#2](https://github.com/Clodmony/starship-coop/issues/2)) and Windows Release hash checks
([#145](https://github.com/Clodmony/starship-coop/issues/145)). The owner may cut a **pre-release** earlier (a name with
`alpha`, `beta` or `rc`); its notes then say which condition is not done, and the first testers' runs are those runs.

1. **The code is on `coop`** (the combined test build PR merged on the owner's word).
2. **Gate on that commit, on the owner's PC:** `python tools/coop/test_all.py --mixed --base-ref <previous gated commit>` must end in
   `RESULT: PASS`. Put the record in `docs/coop/QA.md`.
3. **Notes:** copy `docs/coop/releases/TEMPLATE.md` to `docs/coop/releases/<tag>.md`, fill in every `TODO`, write the gated commit's
   hash and the gate's result into its first two lines, and merge it. Only documentation (`docs/`, `README.md`) may change between the
   gated commit and the tag; `release_check.py` fails on any other file and says to gate again. Also edit the README's "no download yet"
   text when the first release is published.
4. **Tag and push** (the owner's word): `git tag -a vX.Y.Z-coop-alpha1 <commit>` then `git push origin vX.Y.Z-coop-alpha1`. The name is
   `vX.Y.Z-coop` for a release, `vX.Y.Z-coop-alpha1` (or `beta`, `rc`) for a pre-release.
5. **The workflow** (`Release`, runs on that tag, or by hand from the tag with `gh workflow run release.yml --ref <tag> -f tag=<tag>`):
   checks the tag and notes, builds Linux and Windows through `coop-build.yml` (the same steps and tests as a test build), scans both
   packages for game data once more, and makes a draft release `<tag>` with `starship-coop-<id>-windows-x64.zip`,
   `starship-coop-<id>-linux-x64.tar.gz` (keeps the executable flag) and `SHA256SUMS.txt`, the notes as its text, and the pre-release
   flag set from the name. A run made again replaces its own draft and never a published release.
6. **Try the draft on a PC that has never seen the repository:** unpack, give it the ROM, host from one PC and join from another. That run is
   also the two-PC run of #2 and the Windows check. Fix what it finds with a new tag (a tag is never moved).
7. **Publish** the draft on GitHub. Nothing else makes a release public.

**Setup once, before the first tag (the owner's action, in the repository's settings):** apply the ruleset `release-tags` (the command above),
and make the environment `release` wait for you. The job that creates the draft names that environment, so with a required reviewer
it does not start until you approve it on the run's page, even for a tag a stolen token pushed:

```sh
uid=$(gh api user --jq .id)   # the id of the account you are logged in as
gh api -X PUT repos/Clodmony/starship-coop/environments/release --input - <<JSON
{"reviewers": [{"type": "User", "id": $uid}], "prevent_self_review": false}
JSON
```

The actions the workflows use are pinned to commit hashes (a tag of someone else's repository can be moved); update them by looking up
the hash of the new tag, never by writing `@v5`.

The workflow is new and has not been run on GitHub when this was written: expect to fix it on its first run, which is why step 6
exists. The other workflows stay disabled; `release.yml` runs on tags only, so it makes no noise on a pull request.

Limits to say in the notes: the Linux package needs the libraries of Ubuntu 24.04; there is no macOS build (upstream's macOS jobs target a
runner image GitHub retired); `starship.o2r` holds shaders and nine HUD textures of unknown origin that the owner accepted
(NOTES.md, 2026-11-06, #26; #357 looks for the origin).
