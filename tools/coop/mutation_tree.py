#!/usr/bin/env python3
"""A second checkout of this commit for the mutation suites, so they run beside the other steps (#290).

    python tools/coop/mutation_tree.py make --path <folder> [--config Debug] [--build-dir build-cmake]
    python tools/coop/mutation_tree.py remove --path <folder>

The mutation suites (session_mutants.py, net_mutants.py, ...) break a source file in place, rebuild their test program
and run its tests, so nothing else may build or read that tree while one runs (tools/coop/mutants.py). In a checkout of
their own they can run at the same time as every other step of test_all.py: 11 of the 41 minutes of a full run were
these suites, one after the other at the end, on an otherwise idle PC.

`make` adds a detached worktree of HEAD at <folder>, applies the tracked changes of this checkout to it (a file that git
does not know yet is not copied), links the submodules to this checkout's, and configures a build folder in it with
build.py --configure-only: the suites build their own test programs from there, nothing else. `remove` unlinks the
submodules first, so no tool can follow a link into this checkout, and then removes the worktree.

Needs symbolic links (Linux, macOS); on Windows test_all.py keeps the suites in the one checkout.
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def git(*args, cwd=ROOT, check=True, **kwargs):
    return subprocess.run(["git", *args], cwd=cwd, check=check, **kwargs)


def submodule_paths():
    """The submodule folders of this checkout, as in .gitmodules."""
    done = git("config", "-f", ".gitmodules", "--get-regexp", r"^submodule\..*\.path$", check=False,
               capture_output=True, text=True)
    return [line.split(None, 1)[1].strip() for line in done.stdout.splitlines() if " " in line]


def unlink_submodules(path):
    for rel in submodule_paths():
        link = path / rel
        if link.is_symlink():
            link.unlink()


def remove(path):
    if not path.exists() and not path.is_symlink():
        return
    unlink_submodules(path)
    git("worktree", "remove", "--force", str(path), check=False, capture_output=True)
    if path.exists():
        shutil.rmtree(path, ignore_errors=True)
    git("worktree", "prune", check=False, capture_output=True)


def make(path, config, build_dir):
    if os.name == "nt":
        print("mutation_tree: needs symbolic links; not on Windows")
        return 1
    remove(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    git("worktree", "add", "--detach", str(path), "HEAD", capture_output=True)
    changes = git("diff", "HEAD", "--binary", capture_output=True).stdout
    if changes:
        done = subprocess.run(["git", "apply", "--whitespace=nowarn"], cwd=path, input=changes, capture_output=True)
        if done.returncode != 0:
            print("mutation_tree: the tracked changes of this checkout do not apply:\n" + done.stderr.decode()[-400:])
            return 1
    for rel in submodule_paths():
        target = ROOT / rel
        if not (target / ".git").exists():
            continue  # not checked out here, so the build here does not need it either
        link = path / rel
        if link.is_symlink():
            link.unlink()
        elif link.is_dir():
            shutil.rmtree(link)
        link.symlink_to(target, target_is_directory=True)
    done = subprocess.run([sys.executable, str(path / "tools" / "coop" / "build.py"), "--configure-only", "--quiet",
                           "--config", config, "--build-dir", build_dir], cwd=path, capture_output=True, text=True)
    if done.returncode != 0:
        print("mutation_tree: configure failed:\n" + (done.stdout + done.stderr)[-800:])
        return 1
    print(f"mutation_tree: ready at {path} ({config}, build folder {build_dir})")
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("action", choices=["make", "remove"])
    p.add_argument("--path", type=Path, required=True)
    p.add_argument("--config", default="Debug")
    p.add_argument("--build-dir", default="build-cmake", help="folder name inside the new checkout")
    args = p.parse_args()
    path = args.path.resolve()
    if path == ROOT or path in ROOT.parents:
        print("mutation_tree: refusing to use this checkout")
        return 1
    if args.action == "remove":
        remove(path)
        return 0
    return make(path, args.config, args.build_dir)


if __name__ == "__main__":
    sys.exit(main())
