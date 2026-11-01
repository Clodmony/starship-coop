#!/usr/bin/env python3
"""Copy .agents/skills to .claude/skills (Claude Code reads the latter; other agents read the former).

Edit skills in .agents/skills only, then run: python tools/coop/sync_skills.py
"""
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
src = ROOT / ".agents" / "skills"
dst = ROOT / ".claude" / "skills"
if dst.exists():
    shutil.rmtree(dst)
shutil.copytree(src, dst)
print("synced .agents/skills -> .claude/skills")
