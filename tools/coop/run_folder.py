"""The run folder of a test tool: one folder for one run, however close together runs start (#69, #104)."""
import itertools
import os
import time
from pathlib import Path

# test_all.py sets this for its steps to a folder of its run: each run folder made here gets an empty file of its name
# there, so that the run deletes the folders its own steps made and no other run's (#283).
OWNED_ENV = "COOP_TEST_ALL_OWNED"


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
    """Notes run as made by a step of the test_all.py run that set OWNED_ENV, if one did. A note that cannot be written
    only means the folder is not deleted while another run is running."""
    owned = os.environ.get(OWNED_ENV)
    if owned:
        try:
            Path(owned).mkdir(parents=True, exist_ok=True)
            (Path(owned) / run.name).touch()
        except OSError:
            pass
