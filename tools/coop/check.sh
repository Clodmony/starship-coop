#!/usr/bin/env bash
# Repository checks for Starship Co-op. CI runs this on every pull request; run it before you push.
# Usage: tools/coop/check.sh
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

fail=0
err() { echo "FAIL: $*"; fail=1; }

# 1. No game data: ROM images and asset archives must never be tracked.
bad=$(git ls-files | grep -Ei '\.(z64|n64|v64|u64|o2r|otr)$' || true)
[ -z "$bad" ] || err "game data tracked (ROMs or asset archives):"$'\n'"$bad"

# 2. No large files (asset dumps, builds, recordings). Limit: 5 MB per file.
while IFS= read -r f; do
  [ -f "$f" ] || continue
  size=$(wc -c < "$f")
  [ "$size" -le 5242880 ] || err "file over 5 MB: $f ($size bytes)"
done < <(git ls-files)

# 3. Skills: .claude/skills must mirror .agents/skills (edit .agents/skills, then run tools/coop/sync-skills.sh).
if ! diff -r .agents/skills .claude/skills > /dev/null 2>&1; then
  err ".claude/skills differs from .agents/skills; run tools/coop/sync-skills.sh"
fi

# 4. Protocol registry: message ids must be unique.
dups=$(grep -Eo '^\| *0x[0-9A-Fa-f]{4} *\|' docs/coop/PROTOCOL.md | tr -d '| ' | tr 'a-f' 'A-F' | sort | uniq -d || true)
[ -z "$dups" ] || err "duplicate message ids in docs/coop/PROTOCOL.md: $dups"

# Info: where the fork diverges from upstream game code.
markers=$(git grep -n "COOP:" -- 'src/*' 'include/*' ':!src/port/coop/*' | wc -l || true)
echo "info: $markers COOP: markers in upstream code (git grep -n \"COOP:\" lists them)"

if [ "$fail" -ne 0 ]; then
  echo "co-op checks: FAILED"
  exit 1
fi
echo "co-op checks: ALL OK"
