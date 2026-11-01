---
name: starship-upstream-sync
description: Merge the latest HarbourMasters/Starship into the Starship Co-op fork without losing co-op changes. Use when asked to sync with upstream, update Starship, or bump libultraship through upstream.
---

# Sync with upstream Starship

1. `git fetch upstream origin`; list what is new: `git log --oneline origin/main..upstream/main`.
2. Record the co-op markers before: `git grep -n "COOP:" > local/coop-markers-before.txt`.
3. `git switch -c sync/upstream-$(date +%F) origin/main` and `git merge --no-ff upstream/main`.
4. Resolve conflicts:
   - In code marked `// COOP:`: keep upstream's new code and re-apply the co-op change on top. Keep the marker.
   - `README.md`: keep ours. `.gitignore`: keep both sides.
   - Anything else: prefer upstream unless a co-op change is clearly involved; ask the owner if unsure.
5. `git submodule update --init --recursive`, build, run `tools/coop/check.sh` and the single-player replay checks in
   `docs/coop/QA.md` when they exist.
6. Compare markers: `git grep -n "COOP:"` against `local/coop-markers-before.txt`. Every marker must still exist
   (line numbers may move). Explain any that changed in the PR.
7. Show the owner the diff summary, then open a PR titled `Sync upstream Starship <date>` listing the upstream range,
   conflicts and how each was resolved. It must be merged with a merge commit, not squashed.

Never push to `upstream`; its push URL is disabled on purpose.
