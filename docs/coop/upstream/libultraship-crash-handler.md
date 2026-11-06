# libultraship crash handler: draft report

Draft for the owner to send (#117). Nothing here has gone upstream. Line numbers come from the commits named below, read
on 2026-11-06. Recheck them if this waits.

## What libultraship asks for

Read at the pinned commit and on `main`: `CONTRIBUTING.md`, `docs/CONTRIBUTING.md` (`main` only), `CODE_OF_CONDUCT.md`,
the README, `docs/`, and the repository's community files on GitHub.

- **Contributions:** it "accepts any and all contributions", by PR, issue, email or Discord. It has no issue or PR
  template. Its other rules are for PRs: pass CI (build, clang-format-14, clang-tidy) and answer review.
- **Conduct:** Contributor Covenant 2.1.
- **AI use:** it has **no written policy**. None of those files mention AI, there is no wiki, and searches of its issues
  and PRs for policy terms found none. Unknown: anything said only on Discord. In practice AI use is stated openly: 22
  of the 159 commits on `main` since 2025-08-01 carry a Claude or Copilot co-author trailer, and PRs 1228 and 1297 each
  say so in a line.
- **This repo:** `AGENTS.md` rule 11 and "Sending fixes upstream" in `docs/coop/MAINTAINING.md` ask us to follow their
  rules and disclose AI use, so the text below discloses it.

Suggested route: one issue on `Kenix3/libultraship`, naming both branches, because fixes there land on `main` and
`port-maintenance` as separate PRs.

Before sending: PR #115 of this repo has to be merged. The #98 evidence linked below is on its branch until then.

---

## Text to send

**Title:** CrashHandler on Linux: `exit()` in `ErrorHandler`, allocation and message box before the log

A crash on Linux can leave the process hanging instead of ending it, and can lose the crash log. We found this in a fork
of Starship while making test runs end cleanly. One part is already fixed on your branches; it is listed because our
runs show the hang it removes.

This report was prepared with AI help (Claude Code): the test runs, the code reading and this text.

| Point | `eaaf9d0` | `main` | `port-maintenance` | State |
|---|---|---|---|---|
| 1a. `exit(1)` in `ErrorHandler` | `:202` | `:216`, and `:153` | `:204` | Still present |
| 1b. `exit(1)` in `ShutdownHandler` | `:206` | gone, `:219-223` | gone, `:207-211` | Already fixed: `f8d1945` (#1114), `766a6ba` (#1188) |
| 2. `backtrace_symbols` allocates, and gets `nMaxFrames` | `:148` | `:161` | `:150` | Still present |
| 3. Message box before the log | `:192`, `:198` | `:205`, `:210` | `:194`, `:200` | Still present |
| 4. `sigaction(SIGKILL, ...)` | `:433` | `:454` | `:560` | Still present |

All lines are in `CrashHandler.cpp`: `src/debug/` at `eaaf9d0`, `src/ship/debug/` on the branches.

- `main`: `759b13401cd6903daa67a00a753066f791622e78`.
- `port-maintenance`: `57e6d409ae5c6eadac45ba5ead1c090822fc7c39`.
- `eaaf9d0fc91e2c400f49ef2a1f8547a691ce4d3c` is what Starship pins (its `main`, `3322643f`). It is on no current branch.
- `release` (`c6ecfdb`) is older than the fix and still has 1b, at `:220`.

**What ran:** Starship with libultraship at `eaaf9d0`, fork commits `95962d04` and `797c678b`, ROM US 1.0, Linux, GCC,
Debug, OpenGL on a software renderer, no window, one game at a time. **Not run:** anything on `main` or
`port-maintenance` (those states come from reading the code), Release builds, a run with a window, Windows and macOS.
These handlers exist on Linux only. Runs in full, in
[`docs/coop/NOTES.md`](https://github.com/Clodmony/starship-coop/blob/coop/docs/coop/NOTES.md): the entries "why the
game did not exit after SIGTERM" and "why a crash did not end a test run".

Below, "What happens" comes from the code, "Seen" from runs, and "Why it matters" is derived from both.

### 1. `exit()` in a signal handler

**What happens.** `exit` runs atexit handlers and static destructors on the interrupted thread while the other threads
go on. In Starship it stops for good in the destructor of the static `audio` (`src/port/audio/GameAudio.h:4`):
`pthread_cond_destroy` waits for the audio thread, which waits on that variable (`src/port/Engine.cpp:370`), and only
the main thread wakes it.

**Seen.**

- Stop signals (1b): none of 72 games exited, all needed SIGKILL. In gdb, 61 of 62 stood in that destructor, under
  `exit`, under `ShutdownHandler`.
- Crashes (1a): SIGSEGV sent by hand hung the same way, through `ErrorHandler`, in 2 of 2. Then 12 of 12 test runs were
  still running 10 s after SIGSEGV, SIGABRT, SIGFPE or SIGILL. So were real faults forced under gdb, 1 of 1 each on the
  main and the audio thread. Those 14 left a crash log. That they stop in the same destructor rests on the stacks above.
- A game with our test code off was still running 3 s after SIGSEGV in 8 of 8 tries.

**Why it matters.** A crash looks like a hang, to the player and to any tool, and the process stays until someone kills
it. The destructor here is Starship's, but any port with a static object that a live thread uses is exposed.

**Suggested change for 1a.** `_exit(1)` in place of `exit(1)`: `signal-safety(7)` lists `_exit`, not `exit`. In our test
runs an atexit handler that leaves through `_exit(1)`, before that destructor, ended 18 of 18 crash signals (slowest
0.37 s) and 18 of 18 forced faults under gdb (slowest 1.59 s), with exit code 1 and the crash log in place. Two limits:

- atexit handlers would no longer run after a crash, so a port that relies on them would notice.
- Release builds log through an async logger (`src/ship/log/Logger.cpp:80` on `main`), and we did not run one. The queue
  has to be drained before `_exit`. #1196 says `spdlog::shutdown()` does that on `main` and no longer does on
  `port-maintenance`.

**Note on 1b.** #1114 and #1188 fit what we saw; we did not run them. `SDL_PushEvent` is not safe in a signal handler
either. SDL's own handler says "We can't send it in signal handler; SDL_malloc() might be interrupted!"
(`src/events/SDL_quit.c:55` on SDL `main` `c58854d6`, `:58` on `SDL2` `cee83f2d`), and `SDL_AddEvent` can allocate
(`src/events/SDL_events.c:1092`). SDL sets a flag and sends the quit event at the next pump. In our test runs a flag
read once a frame ended 25 of 25 with exit code 0, at most 0.06 s after the signal.

### 2. `backtrace_symbols` in `ErrorHandler`

**What happens.** It allocates its result inside the signal handler, before anything is written. It also gets
`nMaxFrames` (4096), not the `size` from the line above, so it reads entries `backtrace` never filled: `arr` is not
initialised.

**Seen.** In the 62nd run above, `free` aborted inside a destructor, and `ErrorHandler` then blocked on a libc lock in
`backtrace_symbols`. The stack was cut at 40 frames. In a small C test of the same call with 5 real frames, the result
took 344 bytes with `size` and 118,968 bytes with 4096.

**Why it matters.** A crash inside `malloc` or `free` can stop on a lock the crashed code holds. That comes before the
log, so the run hangs and leaves no crash log.

**Suggested change.** Pass `size`. To survive a crash in the allocator, the handler has to write something before it
allocates: `backtrace_symbols_fd` does not call `malloc`, and `backtrace(3)` says to load libgcc beforehand. The
traceback loop allocates too (`std::string`, `__cxa_demangle` and `Sprintf`, `:187` to `:199` on `main`), so it would
have to come after that first write.

### 3. The message box comes before the log

**What happens.** `SDL_ShowSimpleMessageBox` blocks until it is closed, and only the `PrintCommon()` after it writes the
log. Until then the report sits in `mOutBuffer`. The box asks the player to upload the logs, which do not hold this
crash yet.

**Seen.** Not run with a window. With no video driver set, in a game that failed while starting up, the handler made 3
connections to the X display and 3 calls to `zenity` or `kdialog`, so this step does real work inside the handler. With
`SDL_VIDEODRIVER=offscreen` it tried neither.

**Why it matters.** If the box never returns, or the player kills the game while it is open, the log is lost.

**Suggested change.** Show the box after `PrintCommon()` and the flush, as the last step before the process ends.

### 4. `sigaction(SIGKILL, ...)`

**What happens.** SIGKILL cannot be caught (`sigaction(2)`), and the result of the call is not read.

**Seen.** In a small C test the same call returned -1 with `EINVAL`.

**Why it matters.** It changes nothing at run time, but it reads as if a kill gets a clean shutdown.

**Suggested change.** Delete the line.
