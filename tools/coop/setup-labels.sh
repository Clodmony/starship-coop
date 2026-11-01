#!/usr/bin/env bash
# Create or update the issue labels on GitHub. Run once after publishing, and again after editing this list.
# Usage: tools/coop/setup-labels.sh <owner>/<repo>
set -euo pipefail
if [ $# -ne 1 ]; then
  echo "usage: $0 <owner>/<repo>" >&2
  exit 2
fi
repo=$1
label() { gh label create "$1" --repo "$repo" --color "$2" --description "$3" --force; }

label "type/work-package" 1d76db "A roadmap item (docs/coop/ROADMAP.md)"
label "type/bug"          d73a4a "Something is broken"
label "type/desync"       b60205 "Players see different game state"
label "type/research"     5319e7 "Find out how something works; result goes to NOTES.md"
label "type/decision"     0e8a16 "Needs a decision record (docs/coop/decisions)"
for area in engine render input ui net sync test build docs; do
  label "area/$area" c5def5 "Area: $area"
done
label "status/claimed"      fbca04 "Someone is working on it (see comments)"
label "status/blocked"      e99695 "Waiting on another issue or decision"
label "status/needs-review" 0075ca "Ready for review"
label "upstream"            bfdadc "Should also go to HarbourMasters/Starship"
label "good first issue"    7057ff "Little context needed"
label "help wanted"         008672 "Extra attention is needed"
