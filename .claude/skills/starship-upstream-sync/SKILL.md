---
name: starship-upstream-sync
description: Bring the latest HarbourMasters/Starship into Starship Co-op without losing co-op changes - fast-forward the main mirror, then merge it into coop through a sync PR. Use when asked to sync with upstream, update Starship, or bump libultraship through upstream.
---

# Sync with upstream Starship

Branches: `main` is an unchanged mirror of upstream Starship; `coop` is the co-op integration branch (decision 0003).

1. **Update the mirror** (admins only; ask the owner before pushing). Add the remote once if missing:
   `git remote add starship https://github.com/HarbourMasters/Starship.git` and
   `git remote set-url --push starship DISABLED`. Then `git fetch starship main` and
   `git push origin starship/main:main`. It must be a fast-forward; if it is refused, someone committed to `main`:
   stop and tell the owner.
2. List what is new for `coop`: `git fetch origin` and `git log --oneline origin/coop..origin/main`.
3. Record the co-op markers before: `git grep -n "COOP:" origin/coop > local/coop-markers-before.txt`.
4. `git switch -c sync/upstream-$(date +%F) origin/coop` and `git merge --no-ff origin/main`.
5. Resolve conflicts:
   - In code marked `// COOP:`: keep upstream's new code and re-apply the co-op change on top. Keep the marker.
   - `README.md`: keep ours. `.gitignore`: keep both sides.
   - Anything else: prefer upstream unless a co-op change is clearly involved; ask the owner if unsure.
6. `git submodule update --init --recursive`, then bring the single-player audit up to date. Do this before the build:
   the build runs `check.py`, which fails while the audit is stale.
   - Run `python tools/coop/sp_audit.py`. With no new site it rewrites `docs/coop/audit/single-player.md`.
   - A site the list does not have stops it: it writes nothing and names each new site with the row it would take its
     verdict from, or `no verdict row`. Read those sites, fix or add their rows in `docs/coop/audit/sp-verdicts.tsv`,
     then run `python tools/coop/sp_audit.py --accept`, which takes them in. That records that the sites were shown,
     not that the verdicts fit.
   - `matches no site` means a row has no sites left: delete the row and rerun.
   - Read the diff of the list.
7. `python tools/coop/build.py --quiet` (must end in `build: OK`) and the single-player replay checks in
   `docs/coop/QA.md` when they exist.
8. Compare markers: `git grep -n "COOP:"` against `local/coop-markers-before.txt`. Every marker must still exist
   (line numbers may move). Explain any that changed in the PR.
9. Show the owner the diff summary, then open a PR into `coop` titled `Sync upstream Starship <date>` listing the
   upstream range, conflicts and how each was resolved. It must be merged with a merge commit, not squashed.

Never push to the `starship` remote (HarbourMasters), and never commit to `main`.
