#!/usr/bin/env bash
# Copy .agents/skills to .claude/skills (Claude Code reads the latter; other agents read the former).
# Edit skills in .agents/skills only, then run this.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
rm -rf .claude/skills
mkdir -p .claude
cp -R .agents/skills .claude/skills
echo "synced .agents/skills -> .claude/skills"
