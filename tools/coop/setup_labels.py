#!/usr/bin/env python3
"""Create or update the issue labels on GitHub. Maintainers only; needs the gh CLI, logged in.

    python tools/coop/setup_labels.py <owner>/<repo>
"""
import subprocess
import sys

LABELS = [
    ("type/work-package", "1d76db", "A roadmap item (docs/coop/ROADMAP.md)"),
    ("type/bug", "d73a4a", "Something is broken"),
    ("type/desync", "b60205", "Players see different game state"),
    ("type/research", "5319e7", "Find out how something works; result goes to NOTES.md"),
    ("type/decision", "0e8a16", "Needs a decision record (docs/coop/decisions)"),
    *[(f"area/{a}", "c5def5", f"Area: {a}")
      for a in ("engine", "render", "input", "ui", "net", "sync", "test", "build", "docs")],
    ("status/claimed", "fbca04", "Someone is working on it (see comments)"),
    ("status/blocked", "e99695", "Waiting on another issue or decision"),
    ("status/needs-review", "0075ca", "Ready for review"),
    ("upstream", "bfdadc", "Should also go to HarbourMasters/Starship"),
    ("good first issue", "7057ff", "Little context needed"),
    ("help wanted", "008672", "Extra attention is needed"),
]


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    repo = sys.argv[1]
    for name, color, desc in LABELS:
        subprocess.run(["gh", "label", "create", name, "--repo", repo, "--color", color,
                        "--description", desc, "--force"], check=True)
    print(f"{len(LABELS)} labels up to date in {repo}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
