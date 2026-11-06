"""The run folder of a test tool: one folder for one run, however close together runs start (#69, #104)."""
import itertools
import time


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
            return run
        except FileExistsError:
            pass
