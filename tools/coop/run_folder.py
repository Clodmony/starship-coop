"""The run folder of a test tool: one folder for one run, however close together runs start (#69, #104)."""
import itertools
import os
import shutil
import time
from pathlib import Path

# test_all.py sets this for its steps to a folder of its run: each run folder made here gets an empty file of its name
# there, so that the run deletes the folders its own steps made and no other run's (#283). A tool that deletes what the
# tools it starts made (OwnedFolders, #308) adds a folder of its own, after an os.pathsep: a folder is noted in each.
OWNED_ENV = "COOP_TEST_ALL_OWNED"
# A tool started by hand has no run above it to note its folders: they are noted here instead (#317), so that a
# test_all.py run in the same checkout, which deletes the folders that are new since it started, leaves them.
HAND = Path(__file__).resolve().parents[2] / "local" / "test-hand"


def make_run_folder(parent, prefix=""):
    """Make parent/<prefix><date>-<time> and return it. Runs can start in the same second, in two processes or in two
    threads of one, and with the same prefix. mkdir either makes the folder or says it is taken, so no two runs get one
    folder and the later one adds a number. Only the last mkdir is retried: a parent that cannot be made is an error."""
    parent.mkdir(parents=True, exist_ok=True)
    stamped = f"{prefix}{time.strftime('%Y%m%d-%H%M%S')}"
    for number in itertools.count(1):
        run = parent / (stamped if number == 1 else f"{stamped}-{number}")
        try:
            run.mkdir()
        except FileExistsError:
            continue
        note_owner(run)
        return run


def note_owner(run):
    """Notes run as made by a step of the test_all.py run, or by the tool with OwnedFolders, that set OWNED_ENV, or in
    HAND when none did: a tool started by hand. A note that cannot be written only means the folder is not deleted while
    another run is running, or, in HAND, that a test_all.py run beside it may delete it."""
    for owned in list(filter(None, os.environ.get(OWNED_ENV, "").split(os.pathsep))) or [str(HAND)]:
        try:
            Path(owned).mkdir(parents=True, exist_ok=True)
            (Path(owned) / run.name).touch()
        except OSError:
            pass


class OwnedFolders:
    """The run folders that this process and every tool it starts make in local through make_run_folder (#308), so
    that a test tool can delete them all when it passes, those of a tool it ran to fail on purpose too. The notes go
    into a folder of their own, added to OWNED_ENV, which the tools it starts inherit. outer is what OWNED_ENV held
    before: a run above this one (test_all.py, or a tool that ran this one) that notes the same folders and keeps or
    deletes them by its own rule. A tool whose folders that run reads after it ended leaves them to it."""

    def __init__(self, local):
        self.local = Path(local)
        self.outer = os.environ.get(OWNED_ENV, "")
        # made before this adds itself: noted by the run above, if there is one, which deletes it if this cannot
        self.notes = make_run_folder(self.local, "test-owned-")
        # started by hand: what it and its tools make is noted in HAND too, as note_owner does for a tool by hand (#317)
        os.environ[OWNED_ENV] = os.pathsep.join([self.outer or str(HAND), str(self.notes)])
        # noted in HAND: started by hand, or below a tool that was; its notes there go with what it deletes
        self.by_hand = not self.outer or str(HAND) in self.outer.split(os.pathsep)

    def made(self):
        """The folders noted so far that are still there. Only folders directly in local and named test-*: a tool
        that makes its runs deeper (net_test.py) notes their bare names, which this does not touch."""
        names = sorted(note.name for note in self.notes.glob("*")) if self.notes.is_dir() else []
        return [self.local / name for name in names if name.startswith("test-") and (self.local / name).is_dir()]

    def delete(self):
        """Delete every folder noted, then the notes."""
        for folder in self.made():
            shutil.rmtree(folder, ignore_errors=True)
            if self.by_hand:
                (HAND / folder.name).unlink(missing_ok=True)
        self.close()

    def close(self):
        """Stop noting here and delete the notes; the folders stay, and a run above still has them in its own."""
        if self.outer:
            os.environ[OWNED_ENV] = self.outer
        else:
            os.environ.pop(OWNED_ENV, None)
        shutil.rmtree(self.notes, ignore_errors=True)
        if self.by_hand:
            (HAND / self.notes.name).unlink(missing_ok=True)
