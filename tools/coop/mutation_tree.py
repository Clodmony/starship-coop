#!/usr/bin/env python3
"""A second checkout of this commit for the mutation suites, so they run beside the other steps (#290), or a built
checkout of another commit for coop-off.

    python tools/coop/mutation_tree.py make --path <folder> [--config Debug] [--build-dir build-cmake]
    python tools/coop/mutation_tree.py make --path <folder> --ref <git ref> --build [--jobs N] [--assets-from <folder>]
    python tools/coop/mutation_tree.py remove --path <folder>

The mutation suites (session_mutants.py, net_mutants.py, ...) break a source file in place, rebuild their test program
and run its tests, so nothing else may build or read that tree while one runs (tools/coop/mutants.py). In a checkout of
their own they can run at the same time as every other step of test_all.py: 11 of the 41 minutes of a full run were
these suites, one after the other at the end, on an otherwise idle PC.

`make` adds a detached worktree of HEAD at <folder>, applies the tracked changes of this checkout to it (a file that git
does not know yet is not copied), links the submodules to this checkout's, and configures a build folder in it with
build.py --configure-only: the suites build their own test programs from there, nothing else. `remove` unlinks the
submodules first, so no tool can follow a link into this checkout, and then removes the worktree.

With --ref the worktree is of that commit, as it is, without this checkout's changes: test_all.py --base-ref makes the
build before a change that way (#290). The submodules are linked all the same, so the ref must have them at the commits
checked out here; when it does not, make says so and stops. --build builds the game there (build.py --no-check, at
a low priority, --jobs as given) and copies the game archives (*.o2r) of --assets-from into its build folder: build.py
does not make them, and they come from the ROM, not from a commit.

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


def other_submodules(ref):
    """The submodules that ref has at another commit than the one checked out here, as lines that say so."""
    lines = []
    for rel in submodule_paths():
        if not (ROOT / rel / ".git").exists():
            continue
        entry = git("ls-tree", ref, "--", rel, capture_output=True, text=True).stdout.split()
        here = git("rev-parse", "HEAD", cwd=ROOT / rel, capture_output=True, text=True).stdout.strip()
        if len(entry) >= 3 and entry[2] != here:
            lines.append(f"{rel}: {entry[2][:10]} in {ref}, {here[:10]} here")
    return lines


def build(path, config, build_dir, jobs, assets_from):
    """The game in the new checkout, at a low priority: it runs beside tests that are on the clock."""
    command = [sys.executable, str(path / "tools" / "coop" / "build.py"), "--quiet", "--no-check", "--config", config,
               "--build-dir", build_dir, *(["--jobs", str(jobs)] if jobs else [])]
    done = subprocess.run(command, cwd=path, capture_output=True, text=True, preexec_fn=lambda: os.nice(10))
    print((done.stdout + done.stderr).strip()[-800:])
    if done.returncode != 0:
        return 1
    archives = sorted(Path(assets_from).glob("*.o2r")) if assets_from else []
    for archive in archives:
        shutil.copy2(archive, path / build_dir / archive.name)  # a copy: a link would break when this checkout goes
    print(f"mutation_tree: copied {', '.join(a.name for a in archives) or 'no game archives'}")
    return 0


def make(path, config, build_dir, ref=None, build_it=False, jobs=None, assets_from=None):
    if os.name == "nt":
        print("mutation_tree: needs symbolic links; not on Windows")
        return 1
    remove(path)
    if ref is not None:
        if git("rev-parse", "--verify", "--quiet", f"{ref}^{{commit}}", check=False, capture_output=True).returncode:
            print(f"mutation_tree: no commit {ref}")
            return 1
        other = other_submodules(ref)
        if other:
            print("mutation_tree: the submodules are linked to this checkout's, and the ref has them at other commits; "
                  "build it yourself:\n  " + "\n  ".join(other))
            return 1
    path.parent.mkdir(parents=True, exist_ok=True)
    git("worktree", "add", "--detach", str(path), ref or "HEAD", capture_output=True)
    changes = git("diff", "HEAD", "--binary", capture_output=True).stdout if ref is None else b""
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
    if build_it:
        if build(path, config, build_dir, jobs, assets_from) != 0:
            print("mutation_tree: the build failed")
            return 1
    else:
        done = subprocess.run([sys.executable, str(path / "tools" / "coop" / "build.py"), "--configure-only", "--quiet",
                               "--config", config, "--build-dir", build_dir], cwd=path, capture_output=True, text=True)
        if done.returncode != 0:
            print("mutation_tree: configure failed:\n" + (done.stdout + done.stderr)[-800:])
            return 1
    print(f"mutation_tree: ready at {path} ({ref or 'HEAD with the changes here'}, {config}, build folder {build_dir})")
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("action", choices=["make", "remove"])
    p.add_argument("--path", type=Path, required=True)
    p.add_argument("--config", default="Debug")
    p.add_argument("--build-dir", default="build-cmake", help="folder name inside the new checkout")
    p.add_argument("--ref", help="check out this commit as it is, without this checkout's changes (default: HEAD with them)")
    p.add_argument("--build", action="store_true", help="build the game there, not only configure")
    p.add_argument("--jobs", type=int, help="parallel jobs of that build (default: all cores)")
    p.add_argument("--assets-from", help="folder whose game archives (*.o2r) are copied into the new build folder")
    args = p.parse_args()
    path = args.path.resolve()
    if path == ROOT or path in ROOT.parents:
        print("mutation_tree: refusing to use this checkout")
        return 1
    if args.action == "remove":
        remove(path)
        return 0
    return make(path, args.config, args.build_dir, args.ref, args.build, args.jobs, args.assets_from)


if __name__ == "__main__":
    sys.exit(main())
