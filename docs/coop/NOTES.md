# Notes

Shared lab notebook. Add findings about the game and the port as you make them, newest at the top of each section,
in the same PR as the work. Each entry: date, issue, then **Observed** (with file:line or a link), **Derived**,
**Guess** and **Unknown** as separate lists. Remove nothing; mark an entry `[wrong, see <date>]` if it turns out
false. Settled knowledge moves into DESIGN.md.

Private notes (your paths, your PC, your logs) go in `local/`, not here.

## Build and run

### 2026-11-06, #54, what SHIP_HOME reaches, and how a replay file can end

Identity: ROM US 1.0, on commit `a69c811a`, Linux, GCC, Debug, OpenGL in software, no window.

Observed:

- `sf64.o2r` is looked up in the app directory only (`src/port/Engine.cpp:78`); `starship.o2r` also falls back to the
  working directory (`src/port/Engine.cpp:79`, `libultraship/src/Context.cpp:511-526`). With `SHIP_HOME` at a folder
  without `sf64.o2r`, and no window, the game exited with code 1 within 2 s and wrote nothing in either folder.
- With `SHIP_HOME` at a folder holding `sf64.o2r`, and the tools passing it through, `default.sav`, `logs/`, `mods/` and
  `starship.cfg.json` landed in that folder, plus `imgui.ini` on the runs of 100 frames or more (not on those of 0 or
  30). Besides the data files copied in, the run folder got only the report and the hash log. Seen through `replay.py`,
  `hash_check.py` and `determinism.py`; the three runs of `hash_check.py` shared that one folder.
- A header-only replay reported `PASS` with 0 frames, and `determinism.py` then printed `det: PASS`, "equal to base on
  all 0 frames". `hash_check.py` failed it only because its poke changed nothing, while all three replays said `PASS`.
- A file cut inside a frame also reported `PASS`, with the whole frames before the cut as `frames`.
- A recording stopped with SIGKILL ended inside a frame in 2 of 3 tries (16 and 32 bytes left over, the third on a frame
  edge). The file is flushed every 60 frames (`src/port/coop/test/replay.c:260`) and in blocks in between. No report is
  written then.
- After SIGTERM a recording game ran its `atexit` handler (report `RECORDED`, whole frames only), yet it had not exited
  40 s later and needed SIGKILL. One run measured this.
- A replay with no frames to play took 17 to 21 s in `replay.py`.

Derived:

- A folder without `sf64.o2r` fails loudly; the dangerous `SHIP_HOME` is one that looks like a real install. A test of
  this bug needs a decoy holding that archive, or it shows nothing.
- With `SHIP_HOME` passed through, the game reads its config from there (`libultraship/src/Context.cpp:181`) and its
  mods too (`src/port/Engine.cpp:110`), so a config a tool writes into the run folder is ignored. Not run for that
  case: `determinism.py`'s `opengl` and `window` variants write one.

Guess:

- Most of those 17 to 21 s is `run_game` waiting 15 s for the game to exit after SIGTERM, before it uses SIGKILL.

Unknown:

- Why the game does not exit after SIGTERM.
- Windows and macOS: none of this ran there. `SHIP_HOME` is read on Linux and macOS only
  (`libultraship/src/Context.cpp:471-486`).

### 2026-11-06, #104, run folders of the test tools

Identity: before the change `797c678b`, ROM US 1.0, Linux, GCC, Debug, OpenGL in software, no window. The games ran on a
build of that commit, the transport tests on the same source.

Observed, before the change, with the real clock and runs started just after a second began:

- `exit_test.py`, two at once: one `PASS`, one `FileExistsError` on `local/test-exit-<stamp>`.
- `player_trace.py run`, two at once: the second stopped with `FileExistsError`. `rails --players 1` did the same
  between the two threads of one process.
- `net_test.py --only lan`, two at once, four times: both printed the same `logs:` line, and in every pair at least one
  run failed, five of the eight: `ran out of time: host False, client True` (three), `8 resends on a clean link` (one)
  and one whose line was not kept. Alone the scenario passed five of five.
- `session_test.py --only sim`, two at once: the same `logs:` line, both passed.
- `net_mutants.py` twice, one after the other, with a build folder that does not exist: the same `logs:` line.

Observed, after the change: the same runs each print a folder of their own, the later one with `-2`. `lan` two at once,
four times: all eight passed. `exit_test.py`, two at once: both `PASS`.

Derived:

- A shared folder is more than overwritten logs: `net_test.py` reads and writes `host.port`, `host.json` and
  `client.json` by name in the scenario's folder (`tools/coop/net_test.py:67-68`, `:80`), so two runs use the same
  files.
- `mutants.py` takes a lock before it names the folder (`tools/coop/mutants.py:237`), so of its runs only one that
  starts after another has ended can land in that folder, and that takes a run that ends within the second.
- `mutants.py` starts the test script with `python -I` (`tools/coop/mutants.py:141`), and `check.py` does so too. `-I`
  keeps the script's own folder off `sys.path`, so a tool started that way has to add `tools/coop` itself to import
  `run_folder`. Removing that line from `net_test.py` turned `run_folder_test.py` red.

Guess:

- `ran out of time` comes from the two runs mixing up those files. No run looked at which file went where.

Unknown:

- Windows and macOS: none of this ran there.

### 2026-11-06, #70, which test variables reach the game

Identity: ROM US 1.0, on commit `95962d04`, Linux, GCC, Debug, OpenGL in software, no window.

Observed, before the change, with a variable left in the shell and a replay of 120 frames unless noted:

- `SF64_COOP_TRACE` through `replay.py play`: the result was `PASS`, and the game made that file outside the run folder.
- `SF64_COOP_HASH_POKE_PLAYER=1` through `hash_check.py run` with one player: a good run failed with `poke at frame 60
  did not change any hash`. Its poke went to a player the run does not have
  (`src/port/coop/sync/state_hash.c:304-308`).
- `SF64_COOP_HASH_PROBE=50` through `replay.py play --hash-log`, 3400 frames of input by rule into Corneria: 18
  `# probe` lines in the hash log, the first at frame 3150.
- `SF64_COOP_TEST_AUDIO_SAMPLES=low` and `SF64_COOP_TEST_NO_FMA3=1` through `player_trace.py run`: each wrote a
  `coop: test switch` line into `logs/Starship.log`. Through `replay.py`, which already cleared them, neither did.
- `SF64_COOP_HASH_PROBE` and `SF64_COOP_HASH_POKE_PLAYER` through `player_trace.py run`: no hash log was written, so
  nothing could show. That tool clears `SF64_COOP_HASH_LOG`, and without it `CoopStateHash_Init` stops
  (`src/port/coop/sync/state_hash.c:440`).
- `player_trace.py run` on a replay that never reaches a level: the replay passes and the tool exits 1 with `empty
  trace`, as the trace only has rows in `GSTATE_PLAY` (`src/port/coop/test/player_trace.c:15`).

After the change, the same runs: no file outside the run folder, `hash: PASS` with the poke in `actors`, no test switch
line and no `# probe` line. `hash_check.py probe --players 2` and `hash_check.py run --players 2 --poke-player 1` still
pass, so what `env_extra` carries gets through.

From the code:

- The game reads ten variables with this prefix: `SF64_COOP_REPLAY_RECORD`, `_REPLAY_PLAY`, `_REPLAY_REPORT`
  (`src/port/coop/test/replay.c:283-285`), `_HASH_LOG`, `_HASH_POKE`, `_HASH_POKE_PLAYER`, `_HASH_PROBE`
  (`src/port/coop/sync/state_hash.c:434-437`), `_TRACE` (`src/port/coop/test/player_trace.c:31`), `_TEST_NO_FMA3` and
  `_TEST_AUDIO_SAMPLES` (`src/port/coop/test/switches.c:21-22`).
- No tracked script, workflow or doc sets one in a shell for a tool to pass on. The tools set `_REPLAY_*`, `_HASH_LOG`,
  `_HASH_POKE` and `_TRACE` themselves, and pass the rest through `env_extra` (`tools/coop/hash_check.py:170`, `:274`,
  `tools/coop/determinism.py:39-41`), which `run_game` applies last.
- Probes run only in `GSTATE_PLAY` (`src/port/coop/sync/state_hash.c:375`), so a replay that stays on the title screen
  shows none. That is why `replay_test.py` has no scenario for `SF64_COOP_HASH_PROBE`.

Derived:

- A leaked variable is not always harmless or always loud. It can leave `PASS` standing, or fail a good run with a
  message that points somewhere else.
- A variable added later is cleared too, as `game_env` in `tools/coop/replay.py` goes by the prefix. To reach the game
  from a tool it has to go through `env_extra`.

Unknown:

- Windows and macOS: none of this ran there. `SF64_COOP_TEST_NO_FMA3` only acts on Windows x64
  (`src/port/coop/test/switches.c:24-33`); here it only logged that it has no effect.

### 2026-11-06, #26, what a co-op build needs to start, and what game data it holds

Run on the game code at `e2cb9f1d` and on `797c678b`. ROM US 1.0, Linux, GCC, Debug, OpenGL on a software renderer, no
window. All runs were local, except where a CI run is named, and no game was started on Windows.

**Observed**

- The code treats `starship.o2r` as optional (`src/port/Engine.cpp:107`), but the game does not start without it. A copy
  of the game in a folder of its own, with only `sf64.o2r`, logs `Failed to load default fragment shader, missing
  f3d.o2r?` (`libultraship/src/graphic/Fast3D/backends/gfx_opengl.cpp:303`) and signal 6, and draws no frame. The process
  had not ended 120 s later, so a player sees a hang. The folder `tools/coop/package.py` makes, given `sf64.o2r` and
  `starship.o2r`, draws frames; given `sf64.o2r` alone it does the same as above.
- That archive is packed from `port/` (target `GeneratePortO2R`, `CMakeLists.txt`): 13 files, the shaders and nine HUD
  textures, about 47 KB unpacked.
- `LocateFileAcrossAppDirs` also looks in the executable's folder (`src/port/Engine.cpp:80`). A game in `build-cmake`
  found that folder's `starship.o2r` with none in its run folder, so a test of "without it" needs the game copied away.
- The menu bar is opaque and everything right of the last menu is one colour; the menu font has 7-pixel cells, and a
  character looks the same in every cell. `tools/coop/version_test.py` relies on both, and needs no OCR.
- A CI-built Linux binary (run 42267003729) does not start on a machine with newer libraries: it is linked against
  Ubuntu 24.04's, and `libzip.so.4`, `libtinyxml2.so.10`, `libspdlog.so.1.12` and `libfmt.so.9` are not found. So a
  packaged game has been started from a local Debug build only, and the Linux artifact needs those versions.
- Inside the game binary: of 360,788 windows of 32 bytes, taken every 64 bytes from the ROM and from every entry of
  `sf64.o2r` (zero runs and padding left out), 92 are found. All 92 are in the binary of `797c678b` too. They are in
  named symbols of compiled C: `devstr*` strings, audio tables, `D_Tex_800DB4B8` (`src/engine/fox_wheels.c:93`, 1024
  bytes), four `*_copy` vertex arrays (`src/engine/water_effect.inc`, 4624 bytes each). Those two files equal the
  upstream mirror.

**Derived**

- An artifact without any `.o2r` cannot start by itself, which is why the package now holds `starship.o2r` (next entry).
- No ROM and no `sf64.o2r` is in the binary. The decompiled tables are, as in any build of this source: the check above
  samples, so it shows what is there, not that nothing else is.

**Unknown**

- Whether the Windows artifact starts. Windows is linked statically (`CMakeLists.txt`, `x64-windows-static`) and upstream
  ships the executable alone, so no DLLs are packaged; not run.

### 2026-11-06, #26, where the files in starship.o2r come from

Compared locally with ROM US 1.0, with `sf64.o2r` and with all 51 compressed ROM segments decompressed. Only counts are
kept here, no game data.

**Decision**

The owner accepted `starship.o2r` in the package, knowing the source of its textures stays unknown (PR #120).
`tools/coop/package.py` packs exactly the 13 tracked files under `port/` itself, with no Torch build: four shaders and
nine HUD textures, `sBoostGaugeArrow0` to `8`. Nothing else may go in, and the names are pinned, so a new file under
`port/` stops the package until someone has looked at it.

**Observed**

- **Not ROM data.** No file, pixel block, row of a frame (55 tested) or small chunk (144 tested) of the nine textures is
  in the ROM in either byte order, in a decompressed segment or in any of the 15,898 entries of `sf64.o2r`. The same
  search found 200 of 200 real game textures.
- **Source unknown.** All nine came in one upstream commit, `903cfc81` (2025-01-04, "Added an option to restore the old
  boost gauge and fixed coin"), and have not changed since. That commit has no pull request and no description, and it
  names no source image or tool. Upstream's tracker has nothing on it.
- **The only statement of origin** is the tooltip of the option that uses them: "Restores the beta boost gauge that was
  seen in some beta footage" (`src/port/ui/ImguiUI.cpp:637`).
- The nine frames are one image, rotated: 23 of 91 opaque pixels are equal in all of them, and frame 0 is 94% equal to
  frame 8 mirrored. A shape search over 1,608 of the 1,945 textures, which traced 11 of 12 rotated test sprites to their
  source, found generic wedges and teardrops, none a needle on a hub.
- The asset table entries around `D_1012290`, the gauge frame the same option draws, run back to back with no gap.
- Upstream's own release workflow ships `starship.o2r` (`.github/workflows/main.yml`), and these files are public in
  this repository and in upstream's.

**Guess**

- From the tooltip alone: the needle was redrawn from footage of the pre-release game. That would not be ROM data, but
  it would reproduce the game's design.

**Unknown**

- Who made the source image, and from what.
- Whether a rotated copy sits in ROM data no asset table lists: the search that covered it failed its own control.
- Any redrawn or recoloured derivative: a shape search cannot rule one out.

### 2026-11-04, #1 (WP-01), Windows build

Identity: ROM US 1.0 (SHA-1 `d8b1088520f7c5f81433292a9258c1184afa1457`, `config.yml:34`), commit `ba0781d7`,
Windows 11 Home 10.0.26200, VS 2022 Build Tools, MSVC 19.44.35229.0 (v143), Windows SDK 10.0.26100.0, CMake 4.4.4,
Python 3.13, Git 2.55, generator "Visual Studio 17 2022", Debug, graphics backend DirectX 11 (default).

Observed:

- `git submodule update --init --recursive`, then `python tools/coop/build.py --assets --quiet` builds everything in
  one go: `build: OK (Windows, Debug, 10m22s)` on a fresh build directory. Outputs: `build/x64/Debug/Starship.exe`,
  `build/x64/sf64.o2r`, `build/x64/starship.o2r`, `build/x64/gamecontrollerdb.txt`.
- The ROM from a common dump was in v64 byte order (16-bit swapped, header `37 80 40 12`) despite a `.n64` file name.
  Its SHA-1 matches no entry in `config.yml`. After swapping every byte pair (header `80 37 12 40`) it matches US 1.0.
  `build.py` expects `baserom.z64` in the repo root and does not check the byte order itself (`tools/coop/build.py:120`).
- On Windows the app directory is the working directory: `GetAppDirectoryPath` returns `"."` unless `NON_PORTABLE` is
  set (`libultraship/src/Context.cpp:458-500`). Started with `local/test-a/` as working directory and the two `.o2r`
  files copied there, the game wrote `starship.cfg.json`, `default.sav`, `imgui.ini`, `logs/` and `mods/` into that
  folder and nothing next to the exe. This is how test instances stay apart from the owner's own setup.
- The game booted to the title sequence (window "Starship (DirectX 11)"), checked by a screenshot after 25 s.
- The game log reports `Failed add SDL game controller mappings from "./gamecontrollerdb.txt"` when that file is not in
  the working directory; it is built into `build/x64/`. Copy it into the test folder to get the mappings.
- Torch logs `[error] > Failed to process ast_audio/audio_setup` while building `sf64.o2r`; the build still ends OK.

Unknown:

- Whether the `audio_setup` error affects sound in game (not listened to), and whether upstream builds show it too.
- Release config, Ninja (`--ninja`), Linux and macOS: not tried in this session.

### 2026-11-06, #71, why the game did not exit after SIGTERM

Identity: ROM US 1.0, on commit `95962d04`, Linux, GCC, Debug, OpenGL in software, no window.

Observed:

- None of 72 games on `95962d04` exited after a stop signal, and all needed SIGKILL. 15 were given 45 s (5 recording,
  5 without a replay, 5 playing), one 180 s, 46 between 5 and 6 s, 10 through `exit_test.py` 10 s. The signal was
  SIGTERM, except for 2 SIGINT and 2 SIGQUIT among those 10.
- gdb at the end of 62 of those runs: in 61 the main thread is in `pthread_cond_destroy`, from the destructor of the
  static `audio` (`src/port/audio/GameAudio.h:4`), from `exit`, from `Ship::ShutdownHandler`
  (`libultraship/src/debug/CrashHandler.cpp:205-207`), on top of the signal frame. The signal had landed in the frame
  limiter's `nanosleep`, in the display list interpreter and in a vector growing in the frame interpolation; the hang
  was the same.
- In 60 of those 61 it destroys `cv_to_thread`, with the audio thread waiting on it (`src/port/Engine.cpp:378`):
  `cv_to_thread.__wrefs` is 12, `cv_from_thread.__wrefs` 4. In the other one it destroys `cv_from_thread`, and the
  waiter is the main thread itself, caught by the signal in `EndAudioFrame` (`src/port/Engine.cpp:436`): 8 and 12.
- In the 16 runs sampled for 45 s or more, the main thread's CPU time did not change after the first 1.5 s.
- The 62nd stood somewhere else. `free` aborted inside `Path::~Path`
  (`src/port/interpolation/FrameInterpolation.cpp:156`), and `Ship::ErrorHandler` then blocked on a libc lock in
  `backtrace_symbols` (`libultraship/src/debug/CrashHandler.cpp:148`). The stack was cut at 40 frames, so its outer end
  was not seen.
- One of the 60 runs with a replay lost its end: no report, and a hash log without its closing line. No stack was taken
  of that run. In the other 59 the report was there, a recording held whole frames and the hash log had its closing
  line. Timed in 56 of them, the report took at most 0.08 s.
- SIGSEGV sent by hand to a running game hung the same way in 2 of 2 tries, through the `exit(1)` of
  `Ship::ErrorHandler` (`libultraship/src/debug/CrashHandler.cpp:202`).
- The fix (`src/port/coop/test/stop_signal.c`) replaces the handler only in a test run: when `SF64_COOP_REPLAY_RECORD`,
  `SF64_COOP_REPLAY_PLAY`, `SF64_COOP_HASH_LOG` or `SF64_COOP_TRACE` is set and not empty. These are read as the code
  that uses them reads them (`src/port/coop/test/replay.c:283-284`, `src/port/coop/sync/state_hash.c:434`,
  `src/port/coop/test/player_trace.c:31`).
- With the fix, in `exit_test.py`, 25 of 25 test runs ended with exit code 0, the slowest 0.06 s after the signal: 5 each
  without a replay, on SIGINT, on SIGQUIT, recording and playing. An earlier version without the gate did the same in
  60 of 60, the slowest 0.32 s. All with a replay kept their report, their closing hash line and, recording, whole
  frames.
- The variables nine games really received, logged by a wrapper in place of the game: `replay.py`, `hash_check.py run`
  and `probe`, `determinism.py` and `player_trace.py` each gave `SF64_COOP_REPLAY_RECORD` or `SF64_COOP_REPLAY_PLAY` and
  `SF64_COOP_REPLAY_REPORT`, plus their own. None of those tools waited out its 15 s: each game took 6 or 7 s.
- A plain game, with none of those variables, was still running 3 s after SIGTERM in 5 of 5 tries, as on `95962d04`
  in 2 of 2. The version without the gate ended it with exit code 0 within 0.03 s, in 2 of 2.
- `replay.py record --frames 60` took 20 s on `95962d04` and 5 s with the fix, in 3 of 3 tries each, which bears out the
  guess in the #54 entry. `replay_test.py` took 58 s with the fix; its row in `tools/coop/README.md` says about 5
  minutes.
- A replay with co-op off, 3406 frames into Corneria, gave a hash log with the same 3406 lines on `95962d04` and on the
  version without the gate.

Derived:

- It is a hang, not a delay. `exit` runs on the interrupted main thread with the audio thread still waiting, and
  glibc's `pthread_cond_destroy` waits for the waiters to leave (`__wrefs` 12 above is one waiter plus the destroy
  request, [nptl/pthread_cond_destroy.c](https://sourceware.org/git/?p=glibc.git;a=blob;f=nptl/pthread_cond_destroy.c)).
  Only `StartAudioFrame` and `AudioExit` wake that thread (`src/port/Engine.cpp:424-430`, `:448-456`), and both run on
  the main thread, which is the one stuck. The 40 s in the #54 entry was just how long someone waited.
- The atexit handlers run first because they were registered during `GameEngine::Create`, after the static destructors
  of file-scope objects. That is why the report was there and the game still hung.
- Stopping the audio thread from an atexit handler would not be enough: the run caught in `EndAudioFrame` waits for
  itself, whatever the audio thread does. The main thread can also be interrupted while it holds `audio.mutex`. Not
  run. And only waking the thread would be worse: the members go in reverse order, so after the two condition variables
  comes a joinable `audio.thread`, which is `std::terminate`.
- `exit` in a signal handler also destroys objects the interrupted main thread was changing. `current_recording` and
  `previous_recording` are file-scope and rebuilt each frame (`src/port/interpolation/FrameInterpolation.cpp:170-171`),
  which fits the 62nd run. So the fix leaves the handler with nothing to do but set a flag, and the game ends at the
  next frame through the path of a closed window (`src/port/Game.cpp:47-51`).
- A plain game keeps all of this, as upstream does, so #71 is closed for the tools, not for a player who kills the game.
  The real fix belongs in libultraship's handler.
- A test run whose main loop is stuck no longer writes its report on SIGTERM: only the main loop reads the flag, where
  the old `exit` ran the atexit handlers from the signal. Such a run ends through SIGKILL without a report. Not run.

Guess:

- The run that lost its report stopped in the same kind of place, before the atexit handlers were done.

Unknown:

- Where that run stood.
- Release builds, Windows and macOS: not run. libultraship installs these handlers on Linux only
  (`libultraship/src/debug/CrashHandler.cpp:416`), so `stop_signal.c` does nothing elsewhere.

### 2026-11-06, #98, why a crash did not end a test run

Identity: ROM US 1.0, on commit `797c678b`, Linux, GCC, Debug, OpenGL in software, no window, one game at a time.

Observed:

- On `797c678b`, 12 of 12 test runs were still running 10 s after a crash signal, in `exit_test.py`: SIGSEGV 6 times
  (without a replay, recording, playing), SIGABRT, SIGFPE and SIGILL twice each. All 12 had their crash log, and their
  report and closing hash line where the run writes them.
- So were real faults, 1 of 1 each: gdb sent the program counter to address 0 on the main thread (`fault-main`) and in
  `AudioPlayerBuffered` on the audio thread (`fault-audio`, thread 49 in gdb, `src/port/Engine.cpp:391`). Both left a
  crash log with `Signal: 11`. The stack of these hangs was not taken: that it is the destructor rests on the #71 entry.
- A crash handler made to stop for good before it writes anything (`fault-stuck`, gdb jumps from `backtrace_symbols`
  to `pause`) was still running 10 s later, 1 of 1.
- The fix (`src/port/coop/test/stop_signal.c`) uses the gate of the stop handler. In a test run it puts a handler in
  front of libultraship's for SIGILL, SIGABRT, SIGFPE and SIGSEGV. That one sets a flag and calls libultraship's, which
  writes the log as before. An atexit handler (`stop_signal.c:55`) sees the flag and leaves with `_exit(1)`.
- With the fix, 18 of 18 crash signals ended with exit code 1, the slowest 0.37 s after the signal, with crash log,
  report, closing hash line and, recording, whole frames. `fault-main` did the same in 4 of 4, slowest 0.47 s, and
  `fault-audio` in 14 of 14, slowest 1.59 s, both under gdb.
- `fault-stuck` ended by SIGALRM in 4 of 4, 5.14 to 5.39 s after the fault, from an alarm of 5 s
  (`stop_signal.c:69`), and left no crash log entry for that crash.
- Stop signals kept their path: 15 of 15 test runs ended with exit code 0, the slowest 0.22 s after the signal.
- A plain game was still running 3 s after SIGSEGV in 8 of 8 tries, with its crash log, and after SIGTERM in 3 of 3.
  `797c678b` did the same after SIGSEGV in 2 of 2. The check behind that fails when it should: run against a test run,
  it gave `exit code 1: a plain game lost upstream's handler` in 3 of 3.
- With `SDL_VIDEODRIVER=offscreen` the crash handler tried no message box: a listener on the socket of a made-up X
  display saw 0 connections and fake `zenity` and `kdialog` were not called, 1 of 1, on `797c678b`. The same listener
  saw 3 connections and the fake dialogs 3 calls in a game with no driver set, which failed while starting up. That
  game ended with exit code 1 and a crash log.
- The system's SDL here is sdl2-compat on SDL3.
- A crashed recording reports `RECORDED` and a crashed playback `FAIL_INCOMPLETE`, as a closed window does. Only the
  exit code and the crash log tell them apart, and `tools/coop/replay.py:166` prints the exit code only when there is
  no report.

Derived:

- The order is what makes this work. atexit handlers run last registered first. The destructor of the static `audio`
  was registered before `main`, `CoopStopSignal_Init` runs at `src/port/Engine.cpp:301`, and `CoopReplay_Init` and
  `CoopStateHash_Init` at `:310` and `:311`. So the test output is written first and `_exit` comes before the destructor.
- `_exit(1)` keeps the exit code of upstream's `exit(1)` (`libultraship/src/debug/CrashHandler.cpp:202`), so a tool can
  tell a crash from a stop.
- The alarm is there for the other hang, the 62nd run of the #71 entry: `Ship::ErrorHandler` calls `backtrace_symbols`
  (`libultraship/src/debug/CrashHandler.cpp:148`), which allocates, so a crash inside `malloc` or `free` can stop on a
  lock the crashed code holds. That happens before the log is written (`:198`), so such a run has no crash log, and
  nothing in this repo can change that without replacing libultraship's handler.
- The alarm is off with a window. `SDL_ShowSimpleMessageBox` (`:192`) also comes before the log, so a limit there could
  kill the game while someone reads the box, and the log would be lost.
- The issue's candidate, never destroying `audio`, would change a plain game too, which the owner's rule from #71 does
  not allow, and would not reach a crash handler that hangs before `exit`.

Guess:

- A real lock hang ends through the alarm the way `fault-stuck` does. `fault-stuck` stands in for it with `pause`; a real
  crash inside `malloc` was not produced.

Unknown:

- A test run with a window: not run. The message box waits there as upstream's does, and the `_exit(1)` should follow
  once it is closed.
- Release builds, Windows and macOS: not run. The handlers exist on Linux only, as in the #71 entry.
- A crash before `src/port/Engine.cpp:301` keeps upstream's path. The run with no driver above is the only such crash
  seen, and it ended by itself.

### 2026-11-04, #1 (WP-01), Linux build

Identity: ROM US 1.0 (SHA-1 `d8b1088520f7c5f81433292a9258c1184afa1457`, `config.yml:34`), commit `ba0781d7`, Linux,
GCC, Debug, graphics backend OpenGL (default).

Observed:

- On a fresh worktree of `origin/coop`, `python tools/coop/build.py --assets --quiet` fetched the submodules and built
  everything: `build: OK (Linux, Debug, 2m23s)`. Outputs: `build-cmake/Starship`, `build-cmake/gamecontrollerdb.txt`,
  and `sf64.o2r` and `starship.o2r` in both the repo root and `build-cmake/`.
- The ROM was in v64 byte order (header `37 80 40 12`, `.n64` name) and its SHA-1 matched no entry in `config.yml`.
  `dd conv=swab` gives header `80 37 12 40` and the US 1.0 hash.
- On Linux the `SHIP_HOME` environment variable sets the app directory (`libultraship/src/Context.cpp:481-486`).
  Without it, and with `NON_PORTABLE` off, the directory is `"."` (`libultraship/src/Context.cpp:500`). With
  `SHIP_HOME=<repo>/local/test-boot` and the two `.o2r` files and `gamecontrollerdb.txt` copied there, the game wrote
  `starship.cfg.json`, `default.sav`, `imgui.ini` and `logs/` into that folder and added nothing to
  `$HOME/.local/share` or `$HOME/.config`.
- The game reached the title screen with no input, checked by reading state through gdb and by a screenshot of the
  game window. Sampled about every 7 s: `gGameState` 2 (`GSTATE_TITLE`), `gTitleState` 2, `sCutsceneState` 1, 2, 4, 5,
  then 0 (`TITLE_SCREEN`) at system frame 3090 (`src/overlays/ovl_menu/fox_title.h:20-26`). No crash.
- The attract sequence plays before the title screen unless `gMainController` is -1 or `gGoToTitle` is set
  (`src/overlays/ovl_menu/fox_title.c:204-211`, `src/overlays/ovl_menu/fox_title.c:2156`).
- That run forced `SDL_VIDEODRIVER=x11` so the window could be captured. A plain launch in a Wayland session picked
  the `wayland` driver and also ran (`gGameState` 2, frames advancing).
- Torch logs `[error] > Failed to process ast_audio/audio_setup` on Linux too. `AudioContextFactory::parse` stores the
  audio tables and then returns `std::nullopt` on its normal path
  (`tools/Torch/src/factories/naudio/v1/AudioContext.cpp:44`), and `Companion` logs every empty result as failed
  (`tools/Torch/src/Companion.cpp:319-320`). All three table lines print before the error. The running game has the
  tables: 66 sequences, 33 sound fonts, 4 sample banks.

Derived:

- The `audio_setup` error is a false alarm in Torch's log, not a sign of missing audio data.

Guess:

- Upstream builds log the same error, because the code is in the Torch submodule. Not checked.

Unknown:

- How the sound actually plays: both runs used `SDL_AUDIODRIVER=dummy`, and nobody has listened to it on Linux.
- Release config and macOS: not tried in this session.

## Engine and players

### 2026-11-06, #12, input source per player: who steers whom

Runs on `origin/coop` at 797c678b and on this branch: ROM US 1.0, Linux, GCC, Debug, OpenGL on a software renderer, no
window, one game per run, no transport. Frames are replay frames. "The gamepad" is the SDL virtual gamepad of
`src/port/coop/test/test_pad.c`; no real pad was attached. The runs are `python tools/coop/input_test.py`.

**What a slot means in co-op now.** `CoopInput_Route` (`src/port/coop/game/input.c`) runs in `GamePreUpdateEvent` and
fills the pad slots by player number: slot i holds what player i does this frame, on every client. The local player has
port 0, every other player its source, and nothing else on the PC reaches a slot. With co-op off it does nothing.

Why by player number, and not a private array per player: game code reads slots directly. It reads
`gControllerPress[gMainController]` for pause and the supply call, and `gControllerHold[player->num]`
(`src/engine/fox_hud.c:6231`). Filled by player number, all of that reads the same input on every client.

For WP-22: a network stream drops in with `CoopInput_SetSource` for each remote player, and a client sends what
`CoopInput_Controller` returns. A stream carries hold and press together, the pair the sender's player used, so a
receiver has nothing to work out again. A sync layer that delays the local player's own input uses
`CoopInput_SetLocalSource`. A source cannot wait, as it is read inside the frame: a sync model that waits for a player
waits before the frame starts. None of this picks a side in decision 0002, which is still proposed.

Observed, runs, before (test commit, replay and gamepad steering opposite ways, port 1 pushing a third way):

- A replay wrote every slot, so a live player beside it did not exist: player 0 had the gamepad's buttons on 1 of 1201
  frames and the replay's decoy button on 1200, and flew the gamepad's way on 0 of 10 half cycles.
- With no replay, port 1 steered player 1: its pad was idle on 1 of 1208 frames and it moved 1350 in its flight box.
- **Three reads gave a player's shot to player 0's buttons**, which routing alone does not cure:
  - `PlayerShot_SearchLockOnTarget`: a search shot of player 1 flew on 0 frames with only its owner holding A. It died
    on the frame it left, unless player 0 held A.
  - `PlayerShot_UpdateBomb`: player 1's bomb left at 5600 and went off at 5610, when player 0 pressed B.
  - `Actor_Update`: the campaign branch runs down `lockOnTimers[TEAM_ID_FOX]` only, so a lock-on of player 1 outlasted
    its owner's A by 50 frames. The timer starts at 20. That run had the other two fixed, as the first bug hid part of
    this one.

Observed, runs, after:

- `live`: player 0 had the gamepad's buttons and stick on 1200 of 1200 frames, two frames later, and flew its way on 10
  of 10 half cycles. Player 1 had its stream exactly, hold and press, on 1200 of 1200 frames and swept 2200 of x.
- `alone`, no replay: the same for player 0. Player 1, with no stream, was idle on 1200 of 1200 frames and moved 0.
- `shots`: a search shot of player 1 flew on 43 frames, all with its owner holding A. The bomb went off at 5618, on its
  owner's press. A lock-on outlasted its owner's A by at most 19 frames.
- `whole` and `solo`: a plain replay still flies every player, and co-op off ignores the gamepad, 1200 of 1200 frames.
- `unit`: 16882 of 16882 checks, every local index from 0 to 3 with every player count. 15 of 15 hand-made breaks of
  `input.c` were caught. Press edges taken from the slot pass at index 0 and fail at index 1: that case cannot show in
  the game yet.
- The rail replay on 797c678b and on this branch, `hash_check.py diff`: no difference on 7506 common rows with co-op off
  and on 7501 with two players, every column. So routing changes nothing a replay already did.
- A stick pushed to 80 reads 78 after libultraship.

Observed, reads:

- Team-wide reads stay on `gMainController`, which is player 0's slot: `Play_Main` for Start and R
  (`src/engine/fox_play.c:7199`, `:7222`, `:7232`, `:7252`), the supply call (`src/engine/fox_360.c:667`), the C-right
  trigger (`src/engine/fox_enmy2.c:2695`), the training call (`src/overlays/ovl_i1/fox_tr360.c:257`) and a cutscene
  timer (`src/overlays/ovl_i6/fox_andross.c:1438`).
- **The pause menu reads `gInputPress`** (`src/engine/fox_hud.c:1345`, `:1471`, `:1475`), and that points at the local
  player after the player loop (`src/engine/fox_play.c:7051-7053`, `:7068-7070`).
- The team has one bomb slot, the last entry of `gPlayerShots` (`src/engine/fox_play.c:3217`). A lock by any player
  kills every search shot in flight (`PlayerShot_FindLockTarget`, `src/engine/fox_beam.c:1748-1752`).
- Rumble written straight to an entry stays there: `gControllerRumbleFlags[gMainController]` has 16 sites and
  `gControllerRumbleFlags[0]` 7 more in the overlays. Only entry 0 is served (see #68 below), so these reach the local
  pad at any local index. `CoopInput_RumbleSlot` moves only what goes through `gControllerRumble`.

Derived:

- Who may pause, skip or call for supplies has no record (`DESIGN.md` section 4). Leaving those reads on player 0 picks
  no new rule: it is what the slots give with no change, and it can be changed site by site.
- At a local index other than 0 the pause menu would read a different pad on each client while Start stays player 0's.
  Under lockstep that desyncs, as the menu can quit the level. Nothing reaches it while `CoopPlayers_LocalIndex` is 0;
  #96 and #30 meet it when they change that.
- Press edges come from a copy of the pad, so they do not see what game code writes to a slot in a frame
  (`src/engine/fox_play.c:5713`, `src/overlays/ovl_i3/fox_zo.c:4741`). Upstream's do, through `sPrevController`. A
  button pressed right after a forced boost can make a press here that it would not make upstream. Not seen in a run.
- A stale `CoopPlayers_IsCoop` after a level means routing also runs in the menus after it. On index 0 that is the same
  input; a client would see the host drive the menus.

Not shown:

- A local index other than 0 in the game: only `unit` runs it, with a copy of `Controller_UpdateInput`.
- Rumble in a run. `unit` checks which entry each player gets.
- A real pad, keyboard and mouse, Windows, macOS, a network. Two clients: every run here is one game.
- A live player in all-range, or anywhere but Corneria's rails. `gRapidFire` in co-op: read, not run.

Unknown:

- Whether a real pad on this PC would reach a test game. None was attached.

### 2026-11-06, #59, secret routes and events that depend on a wingman

Read at `95962d04`. Nothing was run. This describes what the game does; the rule for a human-flown wingman is for a
later record (decision 0005, "Open"), and the open questions for it are at the end.

The event scripts are not in this repo: they are ROM data. They were read from the `.o2r` built from ROM US 1.0 with a
throwaway decoder that follows `tools/Torch/src/factories/sf64/ScriptFactory.cpp`. `CO S59:34` means level Corneria,
script 59, command 34. The tables are the `SF64:SCRIPT` rows of `assets/yaml/us/rev0/ast_*.yaml` (Corneria:
`ast_corneria.yaml:445`), and `ActorEvent_ProcessScript` picks one per level (`src/engine/fox_enmy2.c:1067-1120`).
`z` is the `zPos1` of the object that starts a script. Conditions are in `ActorEvent_ProcessTriggers` (`:2354`).
To look a reference up: `python tools/coop/script_reader.py CO S59:34`, see the #90 entry.

Short version, derived from the lists below: two routes read a wingman directly (Corneria needs Falco to open
Sector Y; Sector X loses its choice when Slippy is shot down), Titania always takes Slippy away, and every medal needs
all three wingmen. Four more branches depend on wingmen only through who shot what. Meteo, Zoness, Macbeth and the way
to Venom read no wingman.

Observed, how a route is chosen:

- `gMissionStatus` starts at 0 (`src/engine/fox_play.c:2880`), except Sector X, which starts at `MISSION_ACCOMPLISHED`
  (`:2882-2884`). The map turns it into the next planet: `MISSION_COMPLETE` (0) gives `dest1`, `MISSION_ACCOMPLISHED`
  gives `dest2`, `MISSION_WARP` gives `warp` (`src/overlays/ovl_menu/fox_map.c:1487-1499`, table at `:214`).
- **A better result opens a choice, it does not force a route.** After anything but `MISSION_COMPLETE` the map offers a
  path change (A at `fox_map.c:6754`, Start at `:6771-6780`), and `Map_ChangePath` swaps between `dest1` and the earned
  route (`:3698-3735`). After `MISSION_COMPLETE` the menu has no such option (`:3579`, `:3602-3603`).
- Retry on the map restores the team from `D_ctx_80177C58` (`Map_RetryCourse`, `fox_map.c:3619-3635`), which is
  written when the player leaves for a level (`:3525-3528`). So a route event or a lost wingman can be retried for a
  life.
- Bolse, Area 6 and Venom set `gNextLevel` themselves and read no wingman for it: Bolse to Venom 1
  (`src/overlays/ovl_i4/fox_bo.c:1656`), Area 6 to Venom 2 (`src/overlays/ovl_i3/fox_a6.c:2928`), Venom 1 to Venom 2
  (`src/overlays/ovl_i1/fox_ve1.c:2522`), Venom 2 to Andross (`src/overlays/ovl_i6/fox_ve2.c:443`).

| Level | `dest1` | `dest2` | `warp` | What earns the other route | Wingman |
|---|---|---|---|---|---|
| Corneria | Meteo | Sector Y | | Kill the Carrier (`ovl_i1/fox_co.c:2200`) | **Direct**: Falco |
| Meteo | Fortuna | | Katina | 7 warp rings (`engine/fox_enmy.c:2446-2452`) | None |
| Sector Y | Katina | Aquas | | `gHitCount >= 100` at clear (`ovl_i6/fox_sy.c:2338`) | Indirect: hit credit |
| Fortuna | Sector X | Solar | | All four of Star Wolf down in time (`ovl_i4/fox_fo.c:210-213`) | Indirect: shooter |
| Katina | Sector X | Solar | | Saucerer core destroyed (`ovl_i4/fox_ka.c:1410`) | Indirect: shooter |
| Sector X | Titania | Macbeth | Sector Z | Starts earned, lost with Slippy; 4 gates warp (`engine/fox_play.c:6898-6905`) | **Direct**: Slippy |
| Zoness | Macbeth | Sector Z | | No searchlight missed (`ovl_i3/fox_zo.c:4960`) | None |
| Macbeth | Bolse | Area 6 | | Railroad switch hit (`ovl_i5/fox_ma.c:3397-3410`) | None |
| Sector Z | Bolse | Area 6 | | Great Fox not hit (`ovl_i4/fox_sz.c:1287`, `:1562`) | Indirect: shooter |

Titania, Aquas and Solar have only `dest1` (Bolse, Zoness, Macbeth).

Observed, Corneria (the verdict on the issue's claim is under Derived):

- Outside debug CVars (`src/port/mods/PortEnhancements.c:95-104`), the only write of `MISSION_ACCOMPLISHED` here is the
  Carrier's death. The Carrier fight needs the path turn at object 649 (z 168296). `Object_Init` frees that item unless
  `gRingPassCount >= 7` and `gTeamShields[TEAM_ID_FALCO] > 0` (`engine/fox_enmy.c:1183-1189`). This is checked once,
  when the item loads.
- The 7 are the seven stone arches, objects 587 to 606 (z 143281 to 154454). Each spawns a ring-check item
  (`fox_enmy.c:1061-1079`, `:1191-1192`), counted at `:2508-2513`. All seven are needed.
- The turn moves `gPlayer[0].xPath` by 7000 (`rot.z` 70 times 100: `fox_enmy.c:1088-1090`, `:2569-2572`). Objects load
  only within 4000 of `gPlayer[0].xPath` (`:439-440`, `:587-590`). Granga is object 763 at x -1449; the Carrier and its
  three `OBJ_ACTOR_TEAM_BOSS` are objects 799 to 802 at x about 7000. Object 627 is a Carrier too, at x -1223, and its
  init makes it a fly-by when `xPath` is near 0 (`ovl_i1/fox_co.c:1673-1677`, `:1761-1767`).
- The rescue is S60 (leader, object 472), S59 (Falco, 473) and three S61 chasers (474 to 476) at z 108560 to 108699.
  S59 hits Falco for 25, 15 and 25, then for 255 at `CO S59:34`, unless `EVC_NO_TARGET` fires first (`CO S59:10`). His
  target is S60, which despawns on `EVC_NO_FOLLOWER` once the chasers are gone (`CO S60:12`, `fox_enmy2.c:2377`,
  `:2637`). 255 is the only scripted hit in the campaign that downs a wingman at full shields; the next largest is 70.
- The CVar `gHit64FalcoDead` zeroes his shields at level start (`engine/fox_play.c:2939-2941`).
- `EVC_PASSED_ALL_RINGS` tests the same two values for scripts (`fox_enmy2.c:2663-2683`; only Corneria tests Falco).
  With both true, Falco flies in with lines 2181, 2185 and 2188 (S14, z 158675) and Peppy's chase S1 and S2 (z 163700)
  is skipped.

Observed, Sector X and Titania:

- Spyborg holds the result in `swork[6]`: 1 at spawn (`ovl_i2/fox_sx.c:533`), copied to `gMissionStatus` at its death
  (`:1096`). From `gBossFrameCount` 2200, with Slippy up, the end of an attack leads to state 85 instead (`:971`,
  `:1031`). That spawns a stand-in `OBJ_ACTOR_SX_SLIPPY` (`:18-38`); at animation frame 75 it sets `swork[6] = 0` and
  `gTeamShields[TEAM_ID_SLIPPY] = -2`, and puts player 0 in `PLAYERSTATE_STANDBY` with the camera on Slippy
  (`:102-123`, `:127-152`).
- `SX S94` (object 997, z 204973) runs the other way round: it restores a Slippy who is not up to 255 and flies him in
  before the boss (`EVOP_RESTORE_TEAM`, `fox_enmy2.c:1359`). No other level script uses that command.
- `-2` is Slippy's own state. It keeps his radio working (`engine/fox_radio.c:91`), and `Play_Setup` leaves it alone
  where `-1` and `0` move on (`fox_play.c:545-553`). The clear cutscene has two versions on it (`fox_sx.c:1877`).
- Titania forces `-2` on every start: `Play_InitLevel` writes it (`fox_play.c:2691`) after `Play_Init` copied the saved
  shields (`:2936`, call at `:3016`). No Titania script spawns him.
- He comes back in the Goras fight: the third destroyed part (`ovl_i5/fox_ti.c:4013-4014`, `:4083-4084`) spawns
  `OBJ_ACTOR_TEAM_BOSS` (`:3808-3813`), whose init sets `gTeamShields[TEAM_ID_SLIPPY] = 255`
  (`engine/fox_hud.c:4832-4835`). The boss code flips him to 1 and back around each of his lines (`fox_ti.c:5053-5134`).

Observed, medals and shooter rules:

- `Play_CheckMedalStatus` needs all three wingmen above 0 and `gHitCount >= n` (`fox_play.c:59-65`). Its result gives
  `gLeveLClearStatus` 2, the medal (`fox_demo.c:1271`, `fox_map.c:1473-1476`, `fox_hud.c:1313`). Venom 2 sets 2 itself
  (`fox_ve2.c:123`).
- `Actor_Despawn` adds to `gHitCount` only when `dmgSource == AI360_FOX + 1` (`fox_enmy.c:1761`). `dmgSource` is
  `sourceId + 1` (`engine/fox_beam.c:696`) and `sourceId` is `player->num` (`fox_play.c:3091`). Other sites have no such
  check (`fox_enmy2.c:226`, `:292`, `:2133`, `:2214`, `:2330`).
- Katina: shots with `sourceId != TEAM_ID_FOX` do not hurt the Saucerer (`fox_beam.c:1007-1009`).
- Fortuna: `gStarWolfTeamAlive` clears with no shooter check (`engine/fox_360.c:999-1027`), and a wingman rescued by
  Fox turns on the Star Wolf member Fox hurt (`:530-537`).
- Sector Z: any missile kill counts toward the 6 that end the level (`fox_360.c:918-919`, `fox_sz.c:58-60`). Falco,
  Peppy and Slippy each get a missile as target while their `iwork[2]` is `AI360_FOX` (`fox_sz.c:308-360`); Katt takes
  Slippy's once she has arrived.
- Aquas sets all three to 255 at its start (`ovl_i3/fox_aq.c:437`) and the map saves that (`fox_map.c:1451-1455`).

Observed, other reads of a wingman:

- Slippy's boss health bar needs him above 0 (`fox_hud.c:3204`). On Venom 1 it is only switched on with him up
  (`ovl_i1/fox_ve1.c:1164-1168`).
- `OBJ_ACTOR_MISSILE_SEEK_TEAM` picks a wingman by shields (`fox_enmy.c:1541-1590`). All-range team actors skip a down
  wingman (`fox_360.c:381`), and his Star Wolf opponent turns on Fox: Leon for Falco, Andrew for Slippy, Pigma for Peppy
  (`:567-575`).
- Start and clear cutscenes leave out a wingman at 0 or below, in every level file (for example `fox_co.c:3663-3670`,
  `fox_andross.c:4181-4187`). Katina's also drops allies by `gKaAllyKillCount` (`ovl_i4/fox_ka.c:1775`). Bolse, Venom 2
  and Andross read wingmen for nothing else but radio.
- `gMissionTeamStatus` records who was up per mission (`fox_hud.c:1278-1292`, `fox_map.c:1658-1670`). It is shown on the
  map (`:5704`) and used in the ranking (`ovl_ending/fox_end2.c:239-241`, `ovl_menu/fox_option.c:3999-4001`).
- Hits refill surviving wingmen at level clear (`fox_hud.c:1131-1144`).
- Katt and Bill read no wingman. They depend on `gLeveLClearStatus`: Bill on Katina (`EVC_KA_CLEAR`, for example
  `SX S90`, `SO S20`), Katt on Zoness (`EVC_ZO_CLEAR`: `MA S10`, `MA S38`; `fox_sz.c:74`). Credits: `fox_end2.c:522`,
  `:590`.
- Every other `if` or `switch` on `gTeamShields` in `src/overlays/ovl_i*/`, `fox_demo.c` and `fox_360.c` was classed by
  its function and is radio, a cutscene, or the downed-wingman logic of #61. `Corneria_CoGranga_1UpCheck` reads the team
  only for its radio line (`fox_co.c:561`).

Observed, level scripts. Fly-ins run only while that wingman's shields are above 0, except the two marked and `SX S94`.
A rescue is a fly-in plus enemies aimed at him. It costs shields if failed, and only `CO S59` can down him from full.

| Level | Fly-ins | Rescues | Content gated on a wingman |
|---|---|---|---|
| Corneria | Falco S4 S9 S14 S59 S64 (no gate), Slippy S17 S51 (no gate), Peppy S1 S32 | Slippy S17 with S16; Falco S59 with S60, S61; Peppy S1 with S2 | S36 (3 enemies, Falco up), S8 (Falco up, after the turn) |
| Meteo | Falco S11 S141-143, Peppy S12 S135 S149, Slippy S21 S37 | Slippy S21 with S46, S84 | S6 (44 objects, Peppy down) and S48 (14, Slippy down, bombs); S15 S23 S34 S45 S52-54 (Peppy up, S34 bombs); S114 gold ring meteor blows up by itself with Falco up |
| Sector X | Falco S1 S26, Peppy S21 S76 S80, Slippy S94 (when not up) | Peppy S80 with S78; Peppy S76 with S93 | S93 (10, gold ring group, Peppy up); S105 (bees, gold ring, Slippy up) |
| Area 6 | Falco S12 S36 S41 S48 S74, Peppy S1 S11 S40 S75, Slippy S13 S21 S42 S76 | Falco S12 with S28, S31; Falco S36 with S26; Peppy S11 with S10, S65-71; Slippy S21 with S23 | S22 (5 missiles) hit all three for 20 near player 0 |
| Sector Y | Falco S17 S58 S71 S90 S97, Peppy S18 S59 S75 S86 S98, Slippy S19 S48 S60 S99 | Slippy S48 with S50, S63, S76; Falco S90 with S89 | S92 (object 444): laser or 1UP, **all three up** |
| Solar | Falco S21 S23, Peppy S3 S25 S26, Slippy S4 S24 | Falco S21 with S22 | The fly-ins and S18 carry scripted `DAMAGE_TEAM` hits |
| Zoness | Falco S7 S8 S22, Peppy S5, Slippy S9 S16 S46 | Slippy S46 with S39; Falco S8 with S24 | |
| Macbeth | Falco S2 S3 S9 S26, Peppy S28 S37, Slippy S4 S27 S50 | Falco S2 with S29, S26 with S43; Slippy S27 with S44, S50 with S56; Peppy S28 with S45 | |
| Titania | Falco S2 S4 S6 S7 S8 S27, Peppy S3 S5 S24 S28 | Falco S6 with S15, S18; Falco S7 with S16, S19 | |
| Venom 1 | Falco S0 S20 S83 S85 S86 S98, Peppy S21 S77 S82 S97, Slippy S1 S22 S88 S99 | | S23 (12) and S96 (2): the z 144135 blasts run only with **all three up** |

Aquas and Andross scripts read no wingman. Fortuna, Katina, Bolse, Sector Z and Venom 2 place no event actors at all:
their wingmen are the actors in `gActors[1]` to `[3]`.

Scripts that only pick a radio line by who is up, for #60: Corneria 5, Meteo 16, Sector X 13, Area 6 3, Sector Y 6,
Venom 1 18, Zoness 5, Macbeth 6, Titania 7. No script uses `EVC_TEAM_COUNT_*` or `EVC_CHASED`. Counted over
scripts placed in the level object lists, so unplaced ones like `CO S81` are out.

Derived:

- **Corneria: the issue's "saving Falco leads to the other route" is half of it.** Saving him is necessary, not
  sufficient: the route also needs all seven arches. The rescue is only one way to keep him, because anything that
  takes his shields to 0 before z 168296 closes the route. And the route is not forced: the map still offers Meteo.
- Once the turn is done, Granga and the Carrier fight exclude each other: each is outside the other path's 4000 window.
- A slow Spyborg fight forces Titania. A fast one, or a Slippy already down, leaves `MISSION_ACCOMPLISHED`: Macbeth,
  with the choice back to Titania. So Slippy's knock-out does not open Titania, it takes the other options away.
- Slippy is missing from Titania even when the player picked it with him healthy.
- Nearly everything reads one value, `gTeamShields[id] > 0`, not an actor. What that value means for a human flyer
  decides most of this list.
- The other half of a rescue is the actor: `EVOP_SET_TEAM_ID` stores it in `gTeamEventActorIndex`
  (`fox_enmy2.c:1262-1267`) and chasers aim at that index (`EVOP_SET_TARGET`, `:1451-1456`; chase timer, `:3239`). With
  no such actor the index is stale.
- `CO S51` and `CO S64` have no gate, so a check on shields alone does not stop those two fly-ins.
- If Falco's actor is not spawned, `CO S59` never reaches its 255 hit, so nothing can fail the rescue and the route
  would rest on the arches alone.
- Ring items are collected per player (`Player_CheckItemCollect`, `fox_play.c:1657-1669`), but the path change moves
  only `gPlayer[0]` (`fox_enmy.c:2515-2571`), and the load window follows only `gPlayer[0].xPath`.
- Kills by player 1 do not count in `Actor_Despawn` today. If kills by players 2 to 4 start to count (DESIGN.md
  section 4, item 5 proposes a team total), the Sector Y route and every medal threshold get easier. If they do not,
  those players cannot help earn them. At Katina they cannot hurt the Saucerer at all.
- Replacing a computer wingman removes his help at Fortuna and Sector Z, and the human's shots count there instead.
  Zoness searchlights and Macbeth switches check no shooter either (`ovl_i3/fox_zo.c:4306`, `ovl_i5/fox_ma.c:3397`).
- DESIGN.md section 4, item 4 treats a route as something a player triggers in the level. The map adds a second
  decision after every earned route, and it reads only `gMainController`.

Guess:

- Meteo's S6 waves (Peppy down) stand in for the S15-style waves (Peppy up), to keep that section equally busy.

Unknown:

- Anything at run time: none of this was played. That covers the frames Falco has in S59, how often computer wingmen
  really kill missiles or Star Wolf, and whether the turn item can be missed (its hitbox is 1200 wide,
  `engine/fox_edata_info.c:82`).
- US 1.1: its yaml lists the same scripts, but only US 1.0 data was decoded.
- `EVC_EXPERT_MODE` branches were not followed.

Open questions for the record, nothing decided here:

1. What does `gTeamShields[id]` hold while a human flies that wingman: their shields, a fixed value, or only up or down?
2. Does a human-flown wingman still get the scripted actor? If not, do chasers go after the human, and who takes
   scripted damage, including the 255 at `CO S59:34`?
3. Corneria: what replaces "Falco is up" when a human flies Falco, and what if that player is down at z 168296?
4. Sector X: is a human Slippy taken out at 2200 boss frames, with the stand-in actor and the camera on him? If not,
   does the slow fight still force Titania?
5. Titania: where is a human Slippy until the third Goras part falls?
6. Medals: what counts as "all three up" with human wingmen?
7. Hit credit: do kills by players 2 to 4 count toward `gHitCount`, and does Sector Y's 100 stay?
8. Katina: may players 2 to 4 hurt the Saucerer?
9. Is the lost computer help at Fortuna and Sector Z acceptable, or does it need balancing?
10. Boss health bar: who sees it when Slippy is human, or down?
11. Who picks on the map (`gMainController`; DESIGN.md section 4, item 4)?
12. `gMissionTeamStatus` and the ranking: does a human wingman count as a wingman there?

### 2026-11-06, #86, who fired: shooter tests and dmgSource in co-op

Identity: ROM US 1.0, game code at `4955ca69` on `797c678b`, Linux, GCC, Debug, OpenGL on a software renderer, no
window, 1 client per run, no transport, input by rule. Red is `2b3ade53`, the same commits without the fix.

Observed in the code:

- Three things write a player into `dmgSource`: a shot (`src/engine/fox_beam.c:697`), a bomb or lock-on blast (`:1954`,
  `:1961`, `:1976`) and a collision (`src/engine/fox_play.c:2191`, `src/engine/fox_tank.c:1422`). Upstream writes
  index + 1.
- Enemy code writes `DMG_SRC_2` in six places. Only one is a hit on a wingman: a seek-team missile
  (`src/engine/fox_enmy.c:1645`). The other five come just before `Actor_Despawn`, so that no player gets the kill
  (`src/overlays/ovl_i3/fox_aq.c:1802`, `:2520`, `src/overlays/ovl_i3/fox_zo.c:3522`, `:3630`, `:3648`).
- `dmgSource` is read as: 1 for Fox, 1 to 4 for a versus player (`src/engine/fox_enmy.c:1757`), 2 or 100 for an enemy
  (`src/engine/fox_hud.c:4569`), and 101 and up for an NPC. `<= AI360_PEPPY + 101` takes players and the team together
  (`src/engine/fox_360.c:1145`). Nothing uses 5 to 99.
- A wingman at a boss reads `dmgType` only with `work_048` at 0 (`src/engine/fox_hud.c:4862-4870`).
- A broken wing sets that player's lasers to single when it next shoots (`src/engine/fox_play.c:3465-3468`).
- The Shogun rule keeps its shooter in `boss->swork[19]` (`src/engine/fox_beam.c:1035`), read at
  `src/overlays/ovl_i6/fox_sy.c:564` and `:602`. The audit pattern cannot see those two reads.

Observed in runs (`player_trace.py shots --players 2`, Corneria all-range, 20 shots carried to wingmen with the radio
free):

| | red `2b3ade53` | fixed `4955ca69` |
|---|---|---|
| `dmgSource` of those hits | 2, on all 20 | 11, on all 20 |
| Radio after them | an enemy-hit line on 19, line 2265 on 1 | no line on all 20 |
| Missile hits on wingmen, radio free | 1, with its enemy-hit line | 4, each with its enemy-hit line |
| Frames with two laser marks | 0 | 56 |

- In the fixed run player 1's lasers went from 2 to 0 at frame 4383, player 0 stayed on 2, and the last frame with two
  marks is 4360.
- In that run the five missile hits stayed unread for 6, 53, 56, 58 and 69 frames. All 22 hits of carried shots, 20 of
  them with the radio free, were read on the next frame.
- With four players on the fixed build, 22 carried shots hit with the radio free, none with an enemy's `dmgSource` or
  line. 4 missile hits had their line, and 202 frames had two marks.
- A second fixed two-player run gave the same results. Its trace equals the first on all 5905 lines it holds; the first
  has 23 more at its end.
- The check on single lasers can fail. With the eight `(gLaserStrength[shot->sourceId] != LASERS_SINGLE)` tests in
  `src/engine/fox_beam.c` reading `[0]`, player 1's lasers dropped at frame 4384 and frame 4399 still had two marks.
- Co-op off, `hash_check.py diff` finds no difference between `797c678b` and the fix on two one-player replays: 9015
  frames with player 0 sweeping and firing in all-range, 8003 through the rail section. With the trace on, the
  all-range replay has 42 frames with two marks and one wingman hit with `dmgSource` 1, and matches the base hashes
  too, so it reaches the changed ground test. The rail replay has 2 frames with one mark and no hit on a wingman.

Derived:

- Co-op needs its own values, because `DMG_SRC_2` cannot be taken back from enemy code: `CoopPlayers_DmgSource` gives
  players 1 and up 10 + index. No rule tests those values, so the only behaviour it changes is the enemy-hit line of
  `ActorTeamBoss_HandleDamage`. The other `AI360_FOX + 1` tests read the same before and after for these players.
- Friendly-fire lines stay player 0's. Players 1 and up get no line, which is what indexes 2 and 3 already got. That
  keeps the choice open, and adding them later is one test per site.

Who owns each rule that stays as it was, with the verdict in `docs/coop/audit/sp-verdicts.tsv`:

| Rule | Where | Owner |
|---|---|---|
| Hit count, drops and Star Wolf hits | `src/engine/fox_enmy.c:1761` | WP-16 (#14), DESIGN.md section 4 item 5 |
| Sector Z missile bonus | `src/engine/fox_360.c:920` | WP-16 (#14) |
| Shogun bonus | `src/engine/fox_beam.c:1035` | WP-16 (#14) |
| Katina ally kills | `src/engine/fox_360.c:992` | WP-16 (#14) |
| Saucerer takes only player 0's shots | `src/engine/fox_beam.c:1008` | WP-16 (#14), question 8 in the #59 entry |
| Star Wolf dodges only player 0's shots, outside Venom 2 | `src/engine/fox_beam.c:809` | WP-16 (#14): it decides how fast they fall, and so Fortuna's route |
| Friendly-fire and collision lines | `src/engine/fox_hud.c:4541`, `:4555`, `src/engine/fox_360.c:1147`, `:1172`, `src/engine/fox_enmy2.c:2257` | WP-17 (#15) |
| Rescue of a wingman | `src/engine/fox_360.c:532`, `:626` | WP-17 (#15) |
| Shots hit player 0 only, in all-range | `src/engine/fox_beam.c:838` | friendly fire, WP-07 (#7) |
| Bomb and lock-on buttons of players 1 and up | `src/engine/fox_beam.c:2076`, `:1793` | WP-14 |

A lock-on blast adds to `gHitCount` for any shooter (`src/engine/fox_beam.c:2284`), so that one count already includes
players 1 and up. It is left as it is, for WP-16.

Guess:

- Player 1's lasers dropped at 4383 because a wing broke. The trace does not log wings.

Unknown:

- Whether Granga or the Carrier fired the missiles in these runs: both can (`src/overlays/ovl_i1/fox_co.c:406`,
  `:1654`).
- Anything outside Corneria. The Saucerer, Star Wolf and Shogun sites were read, not run.
- Whether the friendly-fire lines would fit a human-flown wingman: their text was not read.

### 2026-11-06, #56, audit regenerated after WP-10 and WP-11

Read at `a69c811a`. `python tools/coop/sp_audit.py --check` passes again. This moved line numbers and corrected the
notes below; it is not a recheck. The verdicts stay a starting list: a sample of 45 found 5 wrong, and the `flow` group
is not usable as a checklist yet.

Counts now: 1788 sites in 407 functions in 43 files. By verdict: sim 1074, flow 326, view 223, ok 93, setup 53,
input 19.

Observed:

- **Why two verdict rows matched no site.** WP-10 replaced the last `gCamCount` in `Player_InitializeAll`
  (`src/engine/fox_play.c:2610`) and `Play_Update` (`fox_play.c:7039`, `:7054`) with `CoopPlayers_Count()`, and neither
  function has any other pattern. The `// COOP: was gCamCount` comments are skipped. No site moved to another audited
  function: the code stayed, and the choice of count went to `src/port/coop/game/players.c:45-50`, outside the audit's
  scope. So both rows are gone; what `Player_InitializeAll`'s row said now stands in the `Play_Init` row. All game code
  changes since the audit (`d653ee21`) are in `fox_play.c` and `fox_display.c`: 11 `gCamCount` sites went away (5 in
  `Display_Update`, 2 in `Play_Init`, 2 in `Play_Update`, 1 in `Player_InitializeAll`, 1 in `Play_Main`), none came in.
  The notes of the `Play_Init`, `Play_Main` and `Display_Update` rows still described those loops and were corrected.
- **`gMainController` is not the controller that pressed Start.** It is written in two places only:
  `Title_Setup` (`src/overlays/ovl_menu/fox_title.c:171`) and `Title_UpdateEntry` (`:240`, every title frame). Both
  call `Title_CheckControllers` (`:388-400`), which returns the lowest index with `gControllerPlugged[i] == 1`, or -1.
  It reads no button. All controller reads in `fox_title.c` use that index (`:884`, `:3374`, `:3405-3406`,
  `:3446-3464`), so the title reads no other controller.
- **On this port that index is always 0.** `Controller_UpdateInput` sets `gControllerPlugged[i] = osContGetStatus(i)`
  for all four (`src/sys/sys_joybus.c:69`), and `osContGetStatus(x)` is defined as `true` (`:63`, `// LTODO: Fix this`).
  It runs before `Game_Update` each frame (`src/sys/sys_main.c:241`, `:248`), so `Title_CheckControllers` returns 0
  and its -1 is not reached. Read, not run.
- `Title_Setup`'s old note also said every later read uses `gMainController`. Versus does not: `Player_Update` takes
  `gInputHold`, `gInputPress` and `gControllerRumble` from `player->num` when `gVersusMode` is set
  (`fox_play.c:5887-5895`), and pause reads R per controller (`fox_play.c:7234`).
- **`Play_Main` has no radio input.** Its `gMainController` reads are Start (intro skip on a cleared level, `:7191`;
  pause, `:7214`; unpause, `:7244`) and R while paused (`:7224`), which toggles `gShowReticles[0]`
  (`fox_play.c:7226`; drawn at `src/engine/fox_display.c:2072`). The radio answer is C-right, in
  `ActorAllRange_SpawnSupplies` (`src/engine/fox_360.c:667`) and the `EVC_PRESS_CRIGHT` event condition
  (`src/engine/fox_enmy2.c:2695`); their rows were already right. The same mistake was in the `Play_Main` verdict row.
- **A new site only shows as missing when no row covers it.** Tried with temporary sites, reverted afterwards.
  Shown as missing (`--check` names it, `--todo` lists it, and it stays red after a rerun): a new function in
  `fox_play.c`, `gPlayer->` in `Player_Update` (its rows are per pattern), a new tracked file. Not shown as missing:
  a new function in `fox_co.c` (took `sim`, basis `name`, from the `ovl_i*` row) or `fox_demo.c` (took `flow`), and
  `gPlayer[0]` added to `Player_CheckBounds360` (took that row's `setup` and a note that does not describe it). For
  those, `--check` only calls the file stale and passes after a rerun. A file git does not track yet is not scanned:
  `--check` stays green. 678 of the 1788 sites sit under a `*` row, in 21 files.

Derived:

- WP-14 cannot use `gMainController` to find who pressed Start, and on this port neither it nor
  `gControllerPlugged` says which controllers exist.
- After a rerun, the diff of `single-player.md` is the only place new sites show up. The generated header says so now.

Unknown:

- Which 5 verdicts the sample found wrong, beyond the notes #56 names, and how the verdicts outside the sample stand.
  Nothing here rechecked them.
- Why the `flow` group is not usable as a checklist yet: #56 does not say. 113 of its 326 sites have basis `name`.
- Which physical pad lands on index 0: that is libultraship's mapping, not read here.

### 2026-11-06, #60, which radio lines a wingman says on its own

Read at `95962d04`; nothing was run. "Wingman" means Falco, Slippy and Peppy, the three a player can take (decision
0005); Katt and Bill are left out. Lines are named by message id or described, not quoted. Paths in the tables drop
`src/engine/` and `src/overlays/`.

Scripted lines are ROM data, not source, so they are cited as level, script index and command index (`aiIndex / 2`),
for example Corneria 17.8. The tables are the `SF64:SCRIPT` assets (`assets/yaml/us/rev0/ast_corneria.yaml:445` for
Corneria), picked per level in `ActorEvent_ProcessScript` (`src/engine/fox_enmy2.c:1075-1122`). They were decoded from
a US 1.0 `sf64.o2r` with a throwaway script, not kept. Its command format is `EVENT_CMD` (`include/sf64event.h:92`).
Script counts come from walking each script's branches, so they say what can run, not what a given run plays.
To look a reference up: `python tools/coop/script_reader.py Corneria 17.8`, see the #90 entry.

**Observed: how a line reaches the radio**

- *One door.* Everything in play ends in `Radio_PlayMessage(msg, character)` (`src/engine/fox_radio.c:58-130`). Four
  routes add their own conditions. `AllRange_PlayMessage` needs an idle radio and player 0 not in standby
  (`src/engine/fox_360.c:89-96`). `ActorAllRange_PlayMessage` needs `!gHideRadio`, `gActors[0].state == STATE360_2` and
  player 0 not in standby (`:479-483`). `ActorEvent_SetMessage` needs an idle radio or another speaker
  (`src/engine/fox_enmy2.c:29-34`). The script op `EVOP_PLAY_MSG` needs `gPlayer[0].state == PLAYERSTATE_ACTIVE`, reads
  the speaker from the low 9 bits, and moves on either way, so a skipped line is lost (`:1333-1339`).
- *No queue.* A message starts with a priority character (`src/engine/fox_radio.c:64-78`). While the radio is busy, a
  lower priority is dropped and an equal or higher one replaces the current line (`:43-56`, `:97-99`). Of the 363
  message ids wingmen use, 304 are priority 1, 50 priority 2, the 6 low-shield comfort lines priority 0 and the 3 lines
  for Fox's death priority 3.
- *The radio is game state.* `Radio_Draw` advances `gRadioState` in the draw path (`src/engine/fox_game.c:569`), 31
  lines outside `fox_radio.c` read it, and it is hashed with `gRadioMsgId` (`src/port/coop/sync/state_hash.c:246-247`).
  The 2026-11-04 determinism note covers its link to audio.
- *No wingman on the map.* The planet briefing has its own path without `Radio_PlayMessage`
  (`src/overlays/ovl_menu/fox_map.c:5954`). Its 14 message pairs (`:203-213`) read as the General and Fox.

**Observed: how a line picks its speaker**

| Way | Where |
|---|---|
| Named in the call | Most level, boss and cutscene lines |
| Named in the script | Every `EVOP_PLAY_MSG` |
| The reacting actor | `this->aiType` in `ActorAllRange_*` and `ActorTeamBoss_*`, `this->iwork[12]` in `ActorEvent_*` |
| The target of a chasing enemy | `this->aiIndex`, `engine/fox_360.c:1710-1752` |
| Random living wingman: `RAND_INT(2.9f) + 1` until `gTeamShields[id] > 0`, after checking one is up | `engine/fox_play.c:5844-5848`, `engine/fox_beam.c:2287-2289`, `engine/fox_demo.c:1612-1614`, `ovl_i1/fox_co.c:565-567` |
| First living in a fixed order | `engine/fox_enmy.c:1803-1809` (Peppy, Slippy, Falco), `ovl_i3/fox_a6.c:1031-1036` and Meteo 82 (Falco, Slippy, Peppy) |
| Rotating slots, a slot is skipped if its speaker is down | `D_ctx_80177A10[8]`, `ovl_i6/fox_sy.c:348-370`, `:1305-1325` |
| One-in-four roll, Falco or Bill | `ovl_i4/fox_ka.c:2342-2358` |
| Three calls in one frame | `ovl_i6/fox_ve2.c:467-469`, see Derived |

**Observed: what happens when the speaker is down**

- *The gate.* In `GSTATE_PLAY`, a line for Falco, Slippy or Peppy returns silently when `gTeamShields[id] <= 0` and
  `!= -2` (`src/engine/fox_radio.c:80-95`). No stand-in, no delay. Other speakers are not gated, and neither are the
  `RCID_*_RED` wingman ids, which no call or script uses.
- *-2 means away, not down.* It is only ever written for Slippy: Titania start (`src/engine/fox_play.c:2691`), Sector X
  boss (`src/overlays/ovl_i2/fox_sx.c:113`). The gate lets it speak and the portrait shows no DOWN. What -1 and 0 mean
  is #61.
- *Stand-in lines.* Level-complete cutscenes test `gTeamShields` first and let ROB speak instead: 86 sites in 12
  functions, usually one line for -1 (ship in for repairs or in the docking bay) and one for 0 (repairs complete), for
  example `src/overlays/ovl_i6/fox_sy.c:2353-2370`. `Andross_80193C4C` (`src/overlays/ovl_i6/fox_andross.c:4303-4342`),
  `Katina_LevelComplete` (`src/overlays/ovl_i4/fox_ka.c:2046`) and Slippy in `Titania_LevelComplete`
  (`src/overlays/ovl_i5/fox_ti_cs.c:439`) have none, so the gate just drops the line.
- *Skipped scenes.* `EVC_FALCO_ACTIVE`, `EVC_PEPPY_ACTIVE` and `EVC_SLIPPY_ACTIVE` branch only when `gTeamShields > 0`
  (`src/engine/fox_enmy2.c:2408-2424`). 216 of the 245 reachable scripted lines sit behind one, usually with the whole
  scene: no branch, no teammate actor (Corneria 17). The other 29 rely on the gate; 16 of them are in Aquas, whose init
  sets all three to 255 (`src/overlays/ovl_i3/fox_aq.c:437`).
- *Forced past the gate.* Some code writes `gTeamShields[id] = 1` around the call. Each retreat does this, then writes
  -1 (`src/engine/fox_360.c:1448-1466`, `src/engine/fox_hud.c:4247-4262`, `src/engine/fox_enmy2.c:2077-2090`). So does
  `Aquas_CsLevelComplete`, restoring the old value (`src/engine/fox_hud.c:6064-6088`), and Slippy in the Titania boss
  (`src/overlays/ovl_i5/fox_ti.c:5053-5134`).
- *The portrait.* A wingman speaking with `gTeamShields <= 0`, not -2, flashes DOWN with the wrench, and the box always
  shows a shield bar from `gTeamShields` (`src/engine/fox_radio.c:729-760`). Only a line already playing or forced gets
  that far.

**Observed: story lines.** Nothing here reads how the wingman is flying, only whether it is up.

| Trigger | Where | Reads | Count |
|---|---|---|---|
| Level-start cutscene | `Corneria_LevelStart` (`ovl_i1/fox_co.c:3135`, `:3190`, `:3221`), `Meteo_LevelStart` (`ovl_i2/fox_me.c:2293`, `:2299`), `Area6_LevelStart` (`ovl_i3/fox_a6.c:2565`) | `csTimer`, `gCsFrameCount` | 6 |
| Level-complete cutscene | every `*_LevelComplete*`, `Cutscene_CoComplete2` (`engine/fox_demo.c:1203-1219`), `Aquas_CsLevelComplete`, `Andross_80193C4C`, `func_demo_80048AC0` (`engine/fox_demo.c:25-75`) | `gCsFrameCount` | 59 |
| All-range event clock | `Bolse_UpdateEventHandler` (`ovl_i4/fox_bo.c:241-259`, `:382`), `Fortuna_UpdateEvents` (`ovl_i4/fox_fo.c:126`), `Katina_UpdateEvents` (`ovl_i4/fox_ka.c:2332`, `:2356`), `SectorZ_UpdateEvents` (`ovl_i4/fox_sz.c:578`), `SectorZ_EnemyUpdate` (`:365`), `Venom2_UpdateEvents` (`ovl_i6/fox_ve2.c:176`) | `gAllRangeEventTimer`, `gStarWolfMsgTimer`, actor timers | 14 |
| Boss clock or boss state | 34 functions in `src/overlays/`, for example `Corneria_CoGranga_Update` (`ovl_i1/fox_co.c:691-704`), `SectorY_SyShogun_Update` (`ovl_i6/fox_sy.c:1698-1724`), `Titania_TiGoras_Update` (`ovl_i5/fox_ti.c:4994-5138`) | `gBossFrameCount`, boss `state` and `swork`, hits on a boss part | 86 |
| Level script, no teammate on screen | 10 levels, for example Corneria 33, Zoness 0, Venom 1 39 | spawn position, then script order and `EVC_*` | 143 |

- Script actors spawn when `gPathProgress` reaches their entry in the level's object list
  (`src/engine/fox_enmy.c:630-638`, `:412`); Venom 1 also compares `gPlayer[0].xPath` (`:631-633`). Fortuna, Bolse,
  Katina, Sector Z and Venom 2 place no event actors, so their lines are all in code.
- This group holds the hints too, mostly Peppy's: controls in scripts (Corneria 27, Meteo 133), boss weak points in
  code (`src/overlays/ovl_i1/fox_co.c:700`). All are addressed to Fox.
- `func_demo_80048AC0` is a story slot whose wording reads shields: Slippy and Peppy by their own (above 160, above
  64, else), Falco by his own under 64, then by Fox's (`src/engine/fox_demo.c:25-75`). 19 calls from six levels.
- Slippy's own story in Sector X and Titania runs on boss state in this group (`src/overlays/ovl_i2/fox_sx.c:35`,
  `:109`, `src/overlays/ovl_i5/fox_ti.c:5005-5134`). The route is #59.

**Observed: scripted teammate scenes on rails.** 102 scripted lines are in scripts that bring a wingman's Arwing on
screen as an event actor (`EVID_TEAMMATE`, `EVOP_SET_TEAM_ID`, `src/engine/fox_enmy2.c:1262-1271`).

- 79 are fixed steps of the scene. **The on-rails calls for help are among them**: they play at their step, whatever
  has happened, for example Corneria 17.8, Macbeth 26.8 and 26.25, Area 6 12.8, Zoness 46.7. Beyond the check that the
  wingman is up, nothing reads its state to choose them.
- 23 sit behind a condition that reads how the scene went: `EVC_NO_TARGET` 11 (`src/engine/fox_enmy2.c:2377-2382`),
  `EVC_NOT_CHASED` 9 (`:2788-2792`, fed by the chaser at `:3235-3239`), `EVC_NO_FOLLOWER` 2 (`:2637-2645`),
  `EVC_ATTACK_GROUP_CLEARED` 1 (`:2685-2692`). These are the on-rails thanks and "that was my target" lines, for example
  Corneria 17.33, Macbeth 28.35, Corneria 9.51.
- Levels: Corneria 15, Macbeth 16, Zoness 14, Area 6 12, Titania 11, Solar 10, Sector X 8, Sector Y 6, Meteo 5, Venom 1
  5. The damage steps between the lines (`EVOP_DAMAGE_TEAM`, `src/engine/fox_enmy2.c:1384-1394`) are #59 and #61.

**Observed: lines that react to the wingman's own state.** 63 sites, in the code of the actor that carries the wingman:
`ActorAllRange` in all-range (`src/engine/fox_360.c:375`), `ActorTeamBoss` at bosses (`src/engine/fox_demo.c:861-873`,
`src/engine/fox_enmy.c:465`), `ActorEvent` with a team id on rails.

| Reaction | All-range `engine/fox_360.c` | Boss `engine/fox_hud.c` | On rails `engine/fox_enmy2.c` | Count |
|---|---|---|---|---|
| Shields ran out, leaves the fight | `:1444-1469` | `ActorTeamBoss_Retreat` `:4243-4262` | `ActorEvent_800700A4` `:2071-2094` | 9 |
| Hit by an enemy | `:1185-1224`, only after a chase; wording by shields, 90 and up or below | `:4569-4580`, `dmgSource` 2 or 100 only | scripted | 9 |
| Hit by Fox's shot, or ran into something | `:1129-1140`, `:1172-1184` | `:4541-4567` | `:2257-2285` | 18 |
| Enemy on its tail, the call for help | `:1705-1752`, also sets the help marker `gTeamHelpActor` (`engine/fox_display.c:47`) | none | scripted | 9 |
| Rescued by Fox, thanks | `:530-552`, `:625-643` | none | scripted | 6 |
| Fox shot the enemy it called | `:1042-1053` | none | scripted | 3 |
| Near Fox for 200 frames, small talk; wording reads Fox's shields | `:1395-1433` | none | none | 9 |

**Observed: lines about the wingman's own action.** 19 sites, all-range only: attack call
(`src/engine/fox_360.c:1781-1806`), enemy shot down (`:2204-2219`, armed at `:1074`, `:1077`), last Star Wolf
(`:1060-1072`), missile down (`:928-937`), and leaving for a missile in Sector Z
(`src/overlays/ovl_i4/fox_sz.c:309-352`).

**Observed: lines that react to Fox, spoken by a wingman.** 21 sites, plus script conditions.

| Trigger | Where | Count |
|---|---|---|
| Player shields under 64, at most every 960 frames | `Player_LowHealthMsg`, `engine/fox_play.c:5834-5875` | 6 |
| Player died | `Cutscene_KillPlayer`, `engine/fox_demo.c:1609-1630` | 3 |
| Lock-on shot worth 7 hits or more, on rails, no boss | `PlayerShot_UpdateShot`, `engine/fox_beam.c:2284-2301` | 3 |
| Enemy dropping `DROP_TEAM_MESG` killed | `Actor_Despawn`, `engine/fox_enmy.c:1802-1809` | 3 |
| Granga one-up check passed | `Corneria_CoGranga_1UpCheck`, `ovl_i1/fox_co.c:561-578` | 3 |
| Enemy on Fox's tail | `engine/fox_360.c:1696`, `:1819` | 2 |
| Third warp ring | `Player_CollisionCheck`, `engine/fox_play.c:2175` | 1 |

Scripted lines reach the same kind of trigger through `EVC_CLOSE_*`, `EVC_HEALTH_0p`, `EVC_30_HITS` and `EVC_80_HITS`
(`src/engine/fox_enmy2.c:2546-2550`, `:2806-2816`), all on `gPlayer[0]` or `gHitCount`.

Counts: 268 code sites name a wingman (165 story, 63 own state, 19 own action, 21 about Fox); 261 script commands do.

**Derived (read, not run)**

- *The owner's split does not follow the code on rails.* In all-range a call for help is an AI reaction. On rails it is
  a script step in the same scene as the story around it, and the scene needs a teammate actor. So what a later record
  says about each scene (#59) decides these lines too.
- *Reaction and action lines depend on the wingman's slot in `gActors`.* They run in its actor's code, or read that slot
  (chase, rescue, Sector Z). With no actor in the slot they should not fire. Story lines and lines about Fox need no
  actor, only `gTeamShields`.
- *Every down check reads `gTeamShields`, not a `Player`.* Whatever co-op keeps there decides whether a human-flown
  character's story lines play and which ROB stand-in appears. Also, `Radio_Draw`'s shield bar shows that value.
- *Dropping a line changes game state.* `gRadioState == 0` gates AI code, including whether the chase block draws
  random numbers (`src/engine/fox_360.c:1705`) and damage lines (`src/engine/fox_hud.c:4537`). A line dropped on one
  client only would change `gRadioState`, the hash and then the simulation.
- *Friendly-fire lines only know player 0.* They test `dmgSource == AI360_FOX + 1`, and `dmgSource` is `sourceId + 1`
  with `sourceId = player->num` (`src/engine/fox_beam.c:696`, `src/engine/fox_play.c:3091`). A shot from player index 1
  gives 2, which is `DMG_SRC_2`, so `ActorTeamBoss_HandleDamage` takes it as an enemy hit (`src/engine/fox_hud.c:4569`).
  Indexes 2 and 3 give 3 and 4, which none of these lines test.
- *Lines about Fox are not limited to Fox.* `Player_LowHealthMsg` runs for every active player
  (`src/engine/fox_play.c:5957`, loop at `:7039-7045`) and its timer is one global, so two active players count it
  twice per frame. `Cutscene_KillPlayer` has no player 0 check. The random pick does not know who is human: a character
  can comfort itself.
- *Scripted lines follow player 0.* While `gPlayer[0]` is not active they are skipped and lost, and `EVC_CLOSE_*`
  measures to `gPlayer[0]`.
- `Venom2_LevelComplete` makes three equal-priority calls in one frame, so the last living of Falco, Peppy, Slippy
  ends up on the radio.

**Guess**

- The `gTeamShields[TEAM_ID_SLIPPY] = 1` writes in the Titania boss look unneeded, since -2 passes the gate anyway.

**Unknown**

- Which lines a real run reaches: counts cover every script a level places, whatever route or branch that needs. 15
  commands sit in scripts no object list places or links to (Corneria 81, Meteo 10, Sector X 60, Solar 27, Venom 1 42,
  63, 91, 106) and 1 is unreachable (Meteo 140.10). These are left out of the 245.
- Whether US 1.1 scripts differ: only US 1.0 was decoded.
- Whether a line can be hidden without moving `gRadioState`: the state machine waits on the voice
  (`src/engine/fox_radio.c:628`), and nothing here tried it.
- How enemies choose a target when a wingman's slot is empty, which the chase lines depend on. Not read.

### 2026-11-05, #6 (WP-06), on-rails: player position, path progress, camera

Read from the source only; nothing was run. Line numbers are for `origin/coop` at 1ec6c677.

**Observed**

- *Two z values per player.* `Player.pos.z` is the player's z in level space; `trueZpos` is `pos.z + camDist`, the
  value collision and shots use (`include/sf64player.h:190`, `:227`; set in `Player_UpdatePath`,
  `src/engine/fox_play.c:3649`; shot hit test `src/engine/fox_beam.c:857`). `camDist` is not 0 in all normal flight:
  `Player_ArwingBoost` pulls it towards -400 while boosting (`src/engine/fox_play.c:5228`; -130 in a meteo warp,
  `:5172`), `Player_ArwingBrake` towards 180 while braking (`:5326`) and back towards 0 on every call (`:5343`). Both
  run each frame for the Arwing (`:5707-5708`). Cutscenes such as `Cutscene_EnterWarpZone` also write it
  (`src/engine/fox_demo.c:469-490`).
- *Path progress is per player, then copied into one global.* `Player_UpdatePath` (`src/engine/fox_play.c:3627`)
  adds `-vel.z` to `player->zPath`, sets `gPathVelZ` and `gPathProgress` from that player, and adds its `zPathVel` to
  the running total `gPathTexScroll` (`:3632-3637`). So every caller overwrites the first two and adds to the third:
  `Player_UpdateOnRails` for the Arwing (`:5716`) and the
  Blue Marine (`:5746`), the Landmaster (`src/engine/fox_tank.c:1159`), cutscenes (`src/engine/fox_demo.c:1886`,
  `src/overlays/ovl_i5/fox_ti_cs.c:358`) and Macbeth (`src/overlays/ovl_i5/fox_ma.c:5544`, `:7043`, `:7679`,
  `:7709`, `:7751`).
- *Forward speed is the player's own.* `Player_MoveArwingOnRails` (`src/engine/fox_play.c:3981`) builds `vel` from
  `baseSpeed`, `boostSpeed` and the stick, then adds `vel.z` to `pos.z` (`:4173`). Boost and brake therefore change
  how fast that player's `zPath`, and with it `gPathProgress`, grows.
- *The flight box follows the path centre.* `pos.x` is clamped to `xPath ± pathWidth`, `pos.y` to
  `yPath + pathFloor … yPath + pathHeight` (`src/engine/fox_play.c:4148-4170`). The box sizes come from
  `Player_Setup` (`:4566`, starting sizes at `:4621-4659`); after that `Player_Update` eases `pathWidth` towards 1100
  for the Arwing and 700 for other forms (`:6182-6195`; `gFovYMode` is only ever 0). `pathWidth` is a half-width.
- *Path turns.* `xPath`/`yPath` ease towards `xPathTarget`/`yPathTarget` in `Player_UpdatePath`
  (`src/engine/fox_play.c:3671-3674`); the step taken this frame goes into the globals `gPathVelX`/`gPathVelY`,
  which effects add to their own velocity (`src/engine/fox_effect.c:2459-2460`, `:2513-2514`, `:3038-3039`).
  `pathChangeTimer` turns the view with `yRot_114`/`xRot_120` (`fox_play.c:3676-3683`). The path-turn and
  path-split items set all of this on **`gPlayer[0]` only** (`src/engine/fox_enmy.c:2550-2591`); Macbeth sets it in
  its own code (`src/overlays/ovl_i5/fox_ma.c:6613-6775`).
- *Level objects load by the global progress.* `Object_LoadLevelObjects` (`src/engine/fox_enmy.c:548`) loads the
  next entries of the level's object list whose `zPos1` lies in `[gPathProgress, gPathProgress + 200]` (`:630`),
  and checks `gPlayer[0]`'s path turn and `xPath` (`:592-595`, `:631-633`). It runs only while `gPlayer[0]` is not
  in the level intro, and culling is switched on by `gPlayer[0].state` (`:3146-3155`).
- *Culling mixes both.* Objects are removed when `gPlayer[0].cam.eye.z + cullDistance < obj.pos.z + gPathProgress`
  (`src/engine/fox_enmy.c:2694`, `:2798`) or when they are too far from `gPlayer[0]`'s `xPath`/`yPath`
  (`:2695-2696`). On rails eye z stays near 400, so the depth test follows the global progress; x and y follow
  player 0.
- *Drawing.* Objects are drawn at `obj.pos.z + gPathProgress` (`src/engine/fox_edisplay.c:983` and many more).
  A player is drawn at `trueZpos + zPath` with **its own** `zPath` (`src/engine/fox_display.c:1519` for the Arwing,
  `:1466` for the Landmaster, `:1500` on foot).
- *Ground scroll.* `Background_DrawGround` scrolls the ground texture from `gPathTexScroll`, not `gPathProgress`
  (`src/engine/fox_bg.c:1418`, `:1538`, `:1645`, `:1738`, `:1813`). Titania draws its ground from
  `gPathGroundScroll` instead (`src/engine/fox_display.c:1951-1957`), which `Player_UpdatePath` sets from the last
  caller when `gGroundType == 4` (`src/engine/fox_play.c:3639-3641`).
- *Floor collision.* `Play_CheckDynaFloorCollision` (`src/engine/fox_play.c:1149`) adds the global `gPathProgress` to
  the z it is given (`:1194`, `:1201`, `:1237`). The player's hit points go through it while `gUseDynaFloor` is set
  (`:2309-2341`; set at `:2713`), and so do shots (`src/engine/fox_beam.c:907`). Ground checks for
  `gGroundType == 4` use the player's own `zPath` instead (`:1800-1801`, `src/engine/fox_tank.c:314`).
- *Camera.* `Camera_Update` (`src/engine/fox_play.c:6664`) picks `Camera_UpdateArwingOnRails` (`:6211`) for the
  Arwing. Eye and look-at x/y follow the player's offset from `xPath`/`yPath`, scaled by `unk_148`/`unk_14C`
  (0.777, set in `Player_Setup`); eye z is 400 plus `sOverheadCamDist`, and 200 more in a somersault (`:6258-6264`),
  look-at z is `trueZpos + gPathProgress - 1`
  (`:6257`). The camera also reads the stick through `gInputPress` (`:6222-6223`). The frame's view is built from
  `gPlayer[gPlayerNum]`: its camera offset, rotated by its `yRot_114`/`xRot_120`, plus its `trueZpos + zPath`
  (`src/engine/fox_display.c:1833`, `:1866-1888`).
- *Update order.* `Play_Update` runs `Player_Update` for every player, then `Object_Update`, then `Camera_Update` for
  every player, all in loops over `gCamCount` (`src/engine/fox_play.c:7033-7045`). Outside versus mode,
  `Player_Update` points `gInputPress`/`gInputHold` at `gMainController` for every player (`:5881-5889`).
- *Wrap.* When `zPath` passes 500000 without a boss, `zPath`, `pos.z` and the object list index are reset
  (`src/engine/fox_play.c:3643-3648`).

**Derived (reasoning, not tested)**

- On rails the world is drawn in "path space": level z plus progress. In flight `pos.z` falls by `vel.z` and `zPath`
  rises by the same amount, so `pos.z + zPath` stays the same. The Arwing is drawn at that depth plus `camDist` in
  front of a camera whose eye sits at z = 400, so boost moves it forward and brake moves it back. Wherever else
  `pos.z` is written on rails (knockback, cutscenes) this can drift.
- With two players the global progress is whatever the last player in the loop wrote, so player 1 would drive object
  loading, dynamic-floor collision, the depth part of culling and every object's draw position, while path turns and
  the x/y part of culling still follow player 0.
- Ground scroll mostly does not follow the last writer: `gPathTexScroll` takes every player's `zPathVel`, so two
  players would scroll the ground at the sum of their speeds, about twice as fast as one. Titania is the exception:
  `gPathGroundScroll` follows the last writer.
- If the two players fly at different speeds (one boosts, one brakes), their `zPath` values drift apart. Each Arwing
  is still drawn at its own depth, but objects are drawn with the shared progress, so the other player would look
  closer than it is and its shots would not line up with where it is drawn. Hit tests against shots compare level z
  (`trueZpos`) and should stay correct. Dynamic-floor collision would not: it adds the shared `gPathProgress` to a
  player's z, so where `gUseDynaFloor` is set, the player whose `zPath` differs from the global is tested against
  the wrong stretch of floor.
- Player 2 has its own `xPath`/`yPath`. Since path items only turn `gPlayer[0]`, player 2's flight box and camera
  would stay on the old path centre after a turn.
- `Camera_Update` does not set `gInputPress` again, so each player's on-rails camera sways with whatever controller
  the last `Player_Update` used.
- Simplest model for WP-12: one shared path. Player 0 (the host or a fixed slot) owns `zPath`, `xPath`, `yPath`,
  the turn state and the speed along the path; the other players copy them each frame and only move freely in x/y
  inside the box. Boost and brake would then either be shared, or become a per-player z offset from the shared
  progress (`camDist` already offsets draw position and hit tests that way). That is a design choice for
  WP-07/WP-12, not decided here.

**Guess**

- The 0.777 factor in the camera is why the camera lags the Arwing towards the box edges; with two players, a camera
  that frames both would need a different factor or a different centre.
- Landmaster and Blue Marine use the same path fields (they call `Player_UpdatePath`), so the shared-path model would
  carry over to Titania, Macbeth and Aquas. Their cameras are separate functions and were not read.

**Unknown**

- Whether boost/brake speed must stay per player for the game to feel right, or can be shared.
- Which other code writes `pos.z` on rails (knockback, damage, somersault `Player_PerformLoop`); not traced.
- How the level intro and the switch from rails to all-range (`PLAYERSTATE_START_360`) set `zPath` and `pos.z` for
  more than one player.
- Cockpit view (`Camera_UpdateCockpitOnRails`, `src/engine/fox_play.c:6287`), tank and Blue Marine cameras: not read.
- Whether the 500000 wrap is ever reached in a normal campaign.

### 2026-11-06, #61, how a downed wingman returns

Read at `95962d04`, nothing was run. This is code reading, plus the level event scripts, which are asset data and not
in the repo: they were read out of an `sf64.o2r` built from ROM US 1.0. It answers the question 0005 left open, and it
describes the game only. The co-op rule is for a later record (DESIGN.md section 4, items 2 and 3).

Short answer: "they miss the next level and return after it" is right for the usual case. A wingman whose shields run
out leaves the level, sits out the whole next level, and is back at full shields (255) in the level after that. It
costs no life. It is not the whole story: Slippy can return inside Sector X, Slippy has a state of his own around
Titania, Aquas refills everyone, the Venom levels do not bring anyone back, and a retry can undo the loss.

Observed:

**What the value means.** `gTeamShields[1..3]` are Falco, Slippy, Peppy (`src/engine/fox_context.c:22-24`).

| `gTeamShields` | Meaning | Source |
|---|---|---|
| 1 to 255 | Flying, with that many shield points | `src/engine/fox_hud.c:1139-1140` |
| 0 | Sitting this level out. Map shows the face with a blinking "OK !"; level-end radio says repairs are complete | `src/overlays/ovl_menu/fox_map.c:6153-6160`, `src/overlays/ovl_i2/fox_sx.c:1889-1891` |
| -1 | Went down in the level just played. Map shows a dim face with a wrench; level-end radio says the ship is in for repairs | `fox_map.c:6144-6151`, `:6172-6173`, `fox_sx.c:1885-1887`, `src/overlays/ovl_i5/fox_ti_cs.c:443-450` |
| -2 | Slippy only: out of play for a story event. Dim face on the map, no wrench, and the radio still lets him talk | `fox_map.c:6145`, `:6172`, `fox_hud.c:197-198`, `src/engine/fox_radio.c:91` |

The radio drops any wingman line while the value is 0 or -1 (`fox_radio.c:91`), so code that needs a line sets the
value to 1 around the call (`src/engine/fox_360.c:1448`, `src/engine/fox_enmy2.c:2077`, `fox_hud.c:4247`). A 1 in the
middle of a retreat is that trick, not a real shield value.

**How a wingman goes down.** A wingman is one of three actor kinds, and each has its own damage and retreat code. All
three end at -1, and none of them explodes: the Arwing climbs away and is removed.

- Event actor (on rails): damage comes off `gTeamShields` directly (`fox_enmy2.c:2121-2122`).
  `ActorEvent_800700A4` then starts `EVSTATE_TEAM_RETREAT`, plays the line and writes -1 (`fox_enmy2.c:2070-2097`,
  `:3335-3339`).
- `ActorTeamBoss`: damage comes off `health`, floored at -1 and copied to `gTeamShields` (`fox_hud.c:4508-4511`,
  `:4535`). The retreat is state 3 (`fox_hud.c:4241-4279`, `:4307-4310`).
- `ActorAllRange`: a hit sets `gTeamDamage`, twice the damage for Slippy and Peppy (`fox_360.c:908-913`).
  `Play_Update` drains that by 2 per frame and writes -1 when shields reach 0 (`fox_play.c:7029-7036`). The retreat is
  `STATE360_6` (`fox_360.c:1444-1470`, `:1473-1495`).
- Scripts can hurt a wingman too, with `EVOP_DAMAGE_TEAM` (`fox_enmy2.c:1384-1391`).

A wingman at 0 or below is not spawned: `fox_360.c:381`, `fox_hud.c:4851-4853`. Intros and event scripts test the
value before they use him (`src/engine/fox_demo.c:163-171`, `EVC_*_ACTIVE` at `fox_enmy2.c:2408-2424`).

**Where the value moves.**

| Stage | What happens to the shields | Source |
|---|---|---|
| New run | All stores set to 255 | `Map_Setup_Menu` `fox_map.c:1578-1589`; `gClearPlayerInfo` `fox_play.c:4921-4924`, set at `src/engine/fox_game.c:419` |
| Leaving for a level | `Play_Setup`: saved -1 becomes 0, saved 0 becomes 255, -2 stays. Skipped for Andross and for Venom 2 phase 2 | `fox_play.c:541-553`, called at `fox_map.c:4294`, `fox_game.c:96` |
| Level load | `gTeamShields` = `gSavedTeamShields`, then `Play_InitLevel` runs after that copy | `fox_play.c:2935-2937`, `:3016` |
| Checkpoint | Saved = current, for the checkpoint item and in Bolse, Fortuna, Katina | `src/engine/fox_enmy.c:2500-2502`, `src/overlays/ovl_i4/fox_bo.c:385-387`, `fox_fo.c:304-306`, `fox_ka.c:944-946` |
| Fox dies, lives left | `PLAY_INIT` again, without `Play_Setup`: everyone goes back to saved | `fox_play.c:6163`, `:7171-7172` |
| Pause, retry | Costs a life. Saved = `gPrevPlanetTeamShields`, with -1 written as 0. Checkpoint cleared, then as above | `fox_hud.c:1475-1480`, `:1538-1539`, `:1551-1552`, `:1587-1594`, `:1600-1607` |
| Level clear screen | Each wingman above 0 gains hit count / 2, capped at 255. Nobody else changes | `fox_hud.c:1097`, `:1131-1151` |
| Level end, to the map | `Map_Setup_Play`: `gPrevPlanetTeamShields` = current with 0 written as 255, saved = current | `fox_map.c:1395-1396`, `:1451-1461` |
| Level end, no map | Same code inline in Bolse and Area 6 | `fox_bo.c:1661-1671`, `src/overlays/ovl_i3/fox_a6.c:2910-2919` |
| Map, go on | `D_ctx_80177C58` = current, the value the next retry goes back to | `fox_map.c:3525-3528`, `:6763-6765` |
| Map, retry course | Costs a life, or game over at 0 lives. `gTeamShields`, saved and `gPrevPlanetTeamShields` = `D_ctx_80177C58`, with 0 written as 255 in the last, then the normal level start | `fox_map.c:3493-3500`, `:3511-3513`, `:3541-3549`, `:3619-3636` |
| Game over | Nothing restored. `gMissionTeamStatus` is written, then ranking, then `GSTATE_INIT` | `fox_play.c:6159-6160`, `fox_map.c:1525-1539`, `:3143-3148`, `src/overlays/ovl_menu/fox_option.c:4508-4512` |
| Continue | Does not exist in the campaign: game over ends the run. `func_pause_800A3E00`, which looks like one, has no caller | `src/engine/fox_pause.c:22-42` |
| Save file | Shields are not saved. Only one alive bit per wingman per mission reaches the ranking record | `include/sf64save.h:24-31`, `fox_option.c:4350-4352` |

The stores: `gSavedTeamShields` is the value the next load copies. `gPrevPlanetTeamShields` is what a pause retry goes
back to. `D_ctx_80177C58` is what a retry from the map goes back to. `gPrevPlanetSavedTeamShields` is written in seven
places and never read.

**Exceptions to the usual case.**

- **Slippy can return inside Sector X.** `EVOP_RESTORE_TEAM` sets a wingman to 255 (`fox_enmy2.c:1359-1360`). Of 19100
  script commands in 1700 script resources, one uses it: `ast_sector_x/D_SX_60320D0_cmd_94`, command 4, for Slippy. That
  script stops at once if `EVC_SLIPPY_ACTIVE` holds; otherwise it restores him and flies him in as an `EVID_TEAMMATE`
  actor. It is placed once, as object id 1094 at index 997 of 1026 in `D_SX_602A164`, at z about 205000, with the boss
  at about 227000 and the only checkpoint at about 83000. It is not in the warp list `D_SX_602F18C`
  (`fox_enmy.c:559-560`).
- **Slippy's -2.** The Sector X boss can knock him out, only after 2200 boss frames and only if he is above 0
  (`fox_sx.c:971`, `:982`, `:102-113`). That also clears `swork[6]`, so the mission ends as `MISSION_COMPLETE`, whose
  next planet is Titania (`fox_sx.c:112`, `:533`, `:1096`, `fox_map.c:1487-1490`, `:271`). `Play_Setup` leaves -2 alone.
  Titania writes -2 on every load, after the copy from saved, whatever his value was (`fox_play.c:2689-2691`). He
  returns at 255 when three of the boss's four parts are gone and `ActorTeamBoss_Init` runs
  (`src/overlays/ovl_i5/fox_ti.c:4013-4014`, `:4083-4084`, `:3808-3813`, `fox_hud.c:4831-4835`).
- **Aquas.** `Aquas_InitLevel` sets all three to 255, after the copy from saved (`src/overlays/ovl_i3/fox_aq.c:437`,
  `fox_play.c:2686`). So everyone leaves Aquas at 255, and a wingman in his sit-out level counts as above 0 there.
- **The Venom levels bring nobody back.** Bolse goes to Venom 1 and Area 6 goes to Venom 2 through `Play_Setup`, which
  runs as usual (`fox_bo.c:1655-1656`, `fox_a6.c:2927-2928`). Venom 1 to Venom 2 phase 2 and Venom 2 to Andross skip its
  conversion (`src/overlays/ovl_i1/fox_ve1.c:2515-2523`, `src/overlays/ovl_i6/fox_ve2.c:436-445`, `fox_play.c:541-543`).
- **A retry can undo it.** See the table: dying goes back to the last save point, a retry goes back to the level start.
- Katt and Bill (ids 4, 5) take no damage (`fox_enmy2.c:2117-2119`), and `Play_Setup` only touches ids 1 to 3. The Hit64
  CVars force a wingman to 0 at every load (`fox_play.c:2939-2947`).

**What being down changes,** beyond the wingman being absent. Routes and radio lines are #59 and #60.

- No medal: `Play_CheckMedalStatus` needs all three above 0 when the level is scored (`fox_play.c:59-66`). That counts
  the sit-out level too.
- The mission's alive bit is cleared for any value at or below 0 (`fox_hud.c:1278-1292`, `fox_map.c:1658-1669`).
- No heal on the clear screen, and Slippy's boss health bar goes with him (`fox_hud.c:3204`).

**Details that matter to sync.** Nothing clears `gTeamDamage` at level load: its only writers are `fox_360.c:910-912`,
`:1467`, `:1479`, `fox_enmy2.c:1390`, `:2091`, `:3338`, and the drain at `fox_play.c:7031`. The heal runs in
`HUD_LevelClearStatusScreen_Draw`, a draw function. `state_hash.c` covers `gTeamShields` only
(`src/port/coop/sync/state_hash.c:241`), not the other stores.

Derived:

- The usual case, as one wingman going down in level N (no checkpoint after it):

| Moment | `gTeamShields` | `gSavedTeamShields` | `gPrevPlanetTeamShields` |
|---|---|---|---|
| Level N, shot down | -1 | value at level start or last checkpoint | unchanged |
| Map after N | -1 | -1 | -1 |
| Leaving the map | -1 | 0 | -1 |
| Level N+1 | 0 | 0 | -1 |
| Map after N+1 | 0 | 0 | 255 |
| Leaving the map | 0 | 255 | 255 |
| Level N+2 | 255 | 255 | 255 |

- The return is a full refill: a wingman who survives on 1 point keeps 1 plus the heal, one who goes down comes back at
  255. The count runs on level changes, not on time.
- Fox's death can bring a wingman back. If he went down after the last save point, the restart copies the older
  positive value. If a checkpoint was reached after he went down, saved is -1 and he stays out, and the usual count
  goes on from there.
- `gPrevPlanetTeamShields` holds the start value of the level being played, and the two rewrites keep it that way: a
  wingman in his sit-out level stays out after a pause retry (-1 written as 0), and one returning stays back (0 was
  written as 255).
- A retry from the map after level N undoes a loss in level N, because `D_ctx_80177C58` still holds the value from
  before level N. The sit-out count is not advanced by retrying.
- Down in Bolse, Area 6, Venom 1 or Venom 2 means out until the end of the run. A wingman sitting out Bolse or Area 6
  is back for the level after it.
- Slippy down at the z of script 94 in Sector X returns at 255, whether he went down earlier in that level or is in his
  sit-out level. It is the only script that can bring back a wingman who went down in the same level. After a death
  past that point he goes back to the checkpoint's value, and the script fires again.
- After Sector X's -2 the only ways on are Titania or a retry: `MISSION_COMPLETE` offers no change of course
  (`fox_map.c:3518-3523`, `:3541-3558`). Titania also holds Slippy captive on a run where he was never knocked out,
  because its -2 does not look at his value. That can happen by changing course to Titania on the map after a
  `MISSION_ACCOMPLISHED` Sector X (`fox_map.c:3698-3702`).
- A wingman at 0 cannot become -1 through scripted damage in normal play, as far as the scripts show: all 35
  script/wingman pairs with `EVOP_DAMAGE_TEAM` test that wingman in the same script. Of 107 pairs with
  `EVOP_SET_TEAM_ID`, 103 test him; the other 4 are `D_CO_603D9E8` 51 and 64, in Corneria where nobody can be in a
  sit-out level, and `D_ME_602F3AC` 136 and `D_SY_6032E18` 33, which are in neither level's object lists. This checked
  that a test exists, not which way it branches.

Guess:

- Script 94 exists so the Sector X boss event, and with it the Titania route, can still happen: `fox_sx.c:971` needs
  Slippy above 0.
- Aquas sets 255 so that the radio and the clear screen work with all three there.

Unknown:

- All of this is from reading. No run has shown it, including script 94 firing with Slippy down.
- Whether Titania can end with Slippy still at -2. If it can, nothing but Aquas or a new run brings him back.
- Whether a value of exactly 0 can stick inside a level. `fox_enmy2.c:2122` can land on 0, and -1 is only written on
  that actor's next update. If the actor is gone by then, the wingman would be read as sitting out and be back one level
  early.
- What a pending `gTeamDamage` does across a restart: the code allows it, its effect in play was not looked at.
- Whether anything other than an object list starts `D_ME_602F3AC` 136 or `D_SY_6032E18` 33.

Open questions for the record that settles this, with lives and respawn (nothing here decides them):

1. Does a human-flown wingman go down through `gTeamShields` at all, or through `Player` shields with `gTeamShields`
   kept in step? Medals, routes, radio, intros and Star Wolf's targets all read `gTeamShields`, so what it says for a
   human matters either way.
2. Does a human who goes down miss a whole level as the game's wingmen do, or return sooner? What does that player do
   and see meanwhile?
3. Does a human wingman going down cost a life from the pool? In the game it costs none, and only Fox's death does.
4. Who triggers a level restart? Today only Fox's death does, for everyone. Does a human wingman's death restart the
   level, and does Fox's death still restore every human wingman to the save point?
5. Who may choose pause retry and retry course? Both spend a life and both can undo a wingman's loss.
6. Does a downed human's return at 255 stay a full refill, and does the clear screen's heal apply to human wingmen?
7. Who flies when the player's wingman is scripted away: Slippy knocked out in Sector X, held captive in Titania, the
   script 94 fly-in, and scripted `EVOP_DAMAGE_TEAM` hits (up to 255 on Falco in `D_CO_603D9E8` 59)?
8. Do the Venom levels keep "out until the end" for a human, who would then watch the final levels?
9. Does one player down cost the whole team the medal, as `Play_CheckMedalStatus` does today?
10. Do humans and computer wingmen follow one rule, or can a run mix two?
11. Which stores join the state hash or the host's snapshot: `gSavedTeamShields`, `gPrevPlanetTeamShields`,
    `D_ctx_80177C58`, `gTeamDamage`?

### 2026-11-06, #90, how to look up a level script reference

Read at `797c678b`. The tool ran on an `sf64.o2r` built from ROM US 1.0, version `76924CA9`. No game ran.

Level scripts are ROM data, so the notes cite them by level, script and command. `tools/coop/script_reader.py` reads
them from your own `sf64.o2r`. Both ways the notes write a reference work:

```sh
python tools/coop/script_reader.py CO S59:34       # the #59 entry: level prefix, script, command
python tools/coop/script_reader.py Corneria 17.8   # the #60 entry: level name, script, command
```

It prints the whole script and marks the command with `>`. A script without a command marks nothing, a level alone
lists its scripts, and no argument lists the levels. Prefixes are the ones in `include/sf64event.h`. In the #59 table
the first column is the level, so `S4` in the Corneria row is `CO S4`, and `S141-143` is three lookups. It finds
`sf64.o2r` in `build-cmake/`, `build/x64/` or the repo root; `--o2r <file>` names another. It reads that one file and
writes nothing. What it prints is game data: keep it out of git, issues and PRs. A reference holds for the ROM it was
read on, so the first line of the output shows the archive's version.

Observed, how a reference maps to the game:

- The script is the slot in the level's table. An object's id minus `ACTOR_EVENT_ID` gives `aiType`
  (`src/engine/fox_enmy.c:412`), and `levelScripts[this->aiType]` is the script (`src/engine/fox_enmy2.c:1122`).
- The command is `aiIndex / 2`. A command is two 16-bit words (`EVENT_CMD`, `include/sf64event.h:92`), and branches
  land on `n * 2` (`fox_enmy2.c:1492`, `:2341`).
- The level picks the table in `ActorEvent_ProcessScript` (`fox_enmy2.c:1075-1121`). Levels with no case use Corneria's
  (`:1118-1120`); the tool refuses those instead of showing Corneria.
- A table entry is a 0x40-byte header, a count, then one 64-bit hash per slot (`ScriptBinaryExporter`,
  `tools/Torch/src/factories/sf64/ScriptFactory.cpp:330-335`, read at `src/port/resource/importers/ScriptFactory.cpp:16-19`).
  The hash is `CRC64()` of the entry's path (`ScriptFactory.cpp:325`). That function returns the value uninverted
  (`tools/Torch/lib/strhash64/StrHash64.cpp:249`), unlike `update_crc64` (`:234`).
- `<table>_cmd_<n>` counts in sorted address order, not table order (`ScriptFactory.cpp:296`, `:302`), so `n` is not
  defined to be the script number. The tool follows the hash, as the game does.
- Some macro parameters read the other way round from the interpreter. `EVENT_F4_PLUS_X(rot, rotVel)` puts `rotVel * 10`
  in arg1 (`include/sf64event.h:106`), while the interpreter takes arg1 as the angle and arg2 / 10 as the step
  (`fox_enmy2.c:1546-1547`, `:3277-3283`). The tool prints what the interpreter reads.

Observed, run on all 14 tables:

- 1700 scripts and 19100 commands decode. Every opcode and enum value has a name in the headers, and in every table
  `n` equals the slot.
- The #59 entry holds up: `CO S59:34`, `CO S59:10` and `CO S60:12` show what it says, all 176 scripts its level table
  names exist, and all 98 fly-ins it lists set that wingman's team id. Only `SX S94` uses `EVOP_RESTORE_TEAM`, the
  largest `EVOP_DAMAGE_TEAM` values are 255 and 70, and no script uses `EVC_TEAM_COUNT_*` or `EVC_CHASED`.
- The references in the #60 entry resolve with their commands in range: 29, found by pattern.
- Two branches land one past the last command of their script: `A6 S26:53` and `VE1 S40:6`.

Derived:

- On the N64 those two targets are the first command of the next script: a script's size is the distance to the next
  pointer (`ScriptFactory.cpp:562`), and the table is in address order. In the port each script is a buffer of its own
  (`src/port/resource/type/Script.cpp:13`), so taking either branch reads past the end of it.

Unknown:

- Whether either branch can fire in play. Nothing was run.
- Whether US 1.1 scripts differ: only US 1.0 was read. `aVe1EventScript` has another offset in rev1
  (`assets/yaml/us/rev1/ast_venom_1.yaml:122`).

### 2026-11-05, #5 (WP-05), single-player assumptions audit

Read at `d653ee21` (branch `coop`). Full list with a verdict per site: `docs/coop/audit/single-player.md`, generated
by `python tools/coop/sp_audit.py` from `docs/coop/audit/sp-verdicts.tsv` (`--check` fails when it is stale or a site
has no verdict). Rerun it after an upstream sync: line numbers move, and new sites show up as missing verdicts
[wrong, see 2026-11-06, #56].

Counts: 1799 sites in 409 functions in 43 files. `gPlayer[0]` 1577, `gMainController` 146, `gCamCount` 73,
`gPlayer->` 3. By verdict: sim 1074, flow 326, view 223, ok 93, setup 64, input 19.

Observed:

- **`gCamCount` doubles as the "versus mode" flag.** Code tests `gCamCount != 1` or `gCamCount == 4` to switch on
  versus rules: player-vs-player hits and mercy rules (`src/engine/fox_play.c`, `Player_CollisionCheck`), floor
  handling (`Player_FloorCheck`), arena wrap at 12000 (`Player_CheckBounds360`), no radio (`src/engine/fox_radio.c`,
  `Radio_Draw`), versus HUD and radar (`src/engine/fox_hud.c`, `HUD_Draw`, `HUD_RadarWindow_Draw`), no positional
  sound (`src/engine/fox_edisplay.c`, `Object_SetSfxSourceToPos`), and the down state going to `PLAYERSTATE_VS_STANDBY`
  instead of a level restart (`fox_play.c`, `Player_Update`). There is a separate `gVersusMode` flag, but these sites
  do not use it.
- **Objects are deleted by player 0's camera.** `Actor_Move` calls `Object_Kill` when an actor is behind player 0's
  `cam.eye.z` plus its cull distance or outside player 0's path bounds (`src/engine/fox_enmy.c`, `Actor_Move`);
  `Effect_Move`, `Scenery_Move`, `Sprite_Move`, `Boss_Move` and `Item_Move` do the same. Level objects are loaded
  relative to player 0's path position (`Object_Load`, `Object_LoadLevelObjects`).
- **Level phases are switched by writing `gPlayer[0].state`.** Bosses and level triggers set player 0 to
  `PLAYERSTATE_LEVEL_COMPLETE`, `PLAYERSTATE_START_360` or `PLAYERSTATE_STANDBY` and move player 0's camera, for
  example `src/overlays/ovl_i1/fox_co.c:109` (Granga), `src/overlays/ovl_i2/fox_me.c:1049` (Crusher),
  `src/overlays/ovl_i4/fox_ka.c:919` (Saucerer), `src/engine/fox_enmy.c`, `ItemPathChange_Update` (path split and level
  end gate) and `src/engine/fox_play.c`, `Play_UpdateLevel`. The audit list marks 18 such boss and enemy functions.
- **Enemy fire targets and hits player 0 only.** Projectile effects aim at, collide with and knock back
  `gPlayer[0]` (`src/engine/fox_effect.c`, `Effect_Effect353_354_Update`, `Effect_Effect398_Update`); homing missiles
  seek player 0 (`fox_enmy.c`, `ActorMissileSeek_Update`); event actors read player 0 position, shields and laser
  level as script conditions (`src/engine/fox_enmy2.c`, `ActorEvent_ProcessTriggers`).
- **Wingmen fly with player 0.** `src/engine/fox_360.c`, `ActorAllRange_Update` and `src/engine/fox_hud.c`,
  `ActorTeamBoss_FlyAlongPlayer`.
- **One controller drives the game.** `gMainController` is set at the title screen to the controller that pressed
  Start [wrong, see 2026-11-06, #56] (`src/overlays/ovl_menu/fox_title.c:171`, `Title_CheckControllers`).
  `Player_Update` then reads `gInputHold`/`gInputPress`/`gControllerRumble` from it for every player
  (`src/engine/fox_play.c:5886-5888`). Pause,
  intro skip and the R-button radio [wrong, see 2026-11-06, #56] (`Play_Main`), bomb detonation and charge-shot
  release (`src/engine/fox_beam.c`)
  read it directly too.
- **Controller index used as player index.** Level code sets rumble with `D_ctx_801779A8[gMainController]` and
  `gControllerRumbleTimers[gMainController]` (for example `src/overlays/ovl_i3/fox_so.c:1652`), while the engine
  reads those arrays by `player->num` (`src/engine/fox_play.c:5891-5897`).
- **One wing animation for all Arwings.** `Display_ArwingWings` draws with `gPlayer[0].jointTable`
  (`src/engine/fox_display.c:633`).
- **Solar heat damages player 0 only** (`fox_play.c`, `Play_UpdateLevel`, the `gPlayer[0].shields--` block).

Derived:

- Setting `gCamCount = 2` for co-op (WP-10) would switch on half of versus mode. WP-10 needs a separate player count
  and must replace the `gCamCount` versus tests with `gVersusMode` or the level id, each one checked.
- Culling and loading are simulation, not drawing: with two players they must use a rule that every client computes
  the same way (for example the leading player, or path progress alone on rails), or clients will delete different
  objects and desync.
- Level phase changes need one place that applies them to all players, which suggests a hook where the game writes
  player 0's state (WP-16, WP-19).
- In co-op the sites marked `view` must use the local player of this client (WP-28), and `input` must use the input
  source of the player concerned (WP-14).

Guess:

- 642 sites have basis `name`: the verdict comes from the function name and file, not from reading the code (mostly
  level enemies and bosses, marked `sim`). A check of those sites for writes to player 0 state or camera found 18
  boss and enemy functions that also switch the level phase; they are now recorded with basis `site`. Other mixed
  functions may remain.
- Most `sim` sites only need "which player" to be chosen (nearest, or a fixed owner); that choice is a game rule
  (WP-07) and the same helper could serve most of them.

Unknown:

- Uses of the player that do not go through these four patterns: `gPlayerNum` outside loops, `Player* player =
  gPlayer` (without `[0]`), `gPlayer[i]` with a fixed `i`, and arrays indexed `[0]` that mean player 0 (`gLaserStrength[0]`,
  `gLifeCount[0]`, `D_ctx_801779A8[0]`). Not counted here.
- `src/port/` (enhancements, cheats, hooks) was out of scope.

### 2026-11-06, #114, --check ignores only line numbers

Read at `797c678b`. Python only: no ROM, no game run. The results on real code come from a scratch clone, the rest from
`python tools/coop/sp_audit_test.py`.

Observed:

- **Before.** One added line above the sites of `src/engine/fox_demo.c` gave `is stale`, exit 1. Two branches that each
  added a comment above that file's first site, each rerun and green alone, merged with no conflict, and the merge
  failed `--check`. A docs-only commit on top failed the same way. This is the failure #114 derived from the code.
- **Not every such pair merges.** Two branches that added a line at lines 85 and 1575 of the same file conflicted in the
  list instead of merging.
- **After.** On such a merged tree `--check` exits 0 and prints a note that only line numbers differ. A plain rerun
  writes the list, and the note goes. A site added or removed in `fox_demo.c` still exits 1, and the last line names the
  command to run: `--accept` for a new site, the plain rerun for a lost one.
- **The key.** Sites are counted per file, function and pattern, so a site that moves to another function is not a moved
  line: it is lost there and new here, also under one verdict row with the row's count unchanged
  (`star-row-site-moved`). A renamed function goes the same way (`star-row-function-renamed`). Both stop the rerun until
  `--accept`, as they did since #73 for the new half.
- **What `--check` compares (owner decision on #114).** The list against what a rerun would write, both read by the same
  parser: site counts, each site's verdict, basis and note, and every line outside the rows. A verdict and a note changed
  in `sp-verdicts.tsv` for `fox_demo.c` each exited 1, named the site as `changed site: ... listed ..., now ...` and the
  plain rerun, and a rerun cleared them. A new row after `--accept` does the same (`no-row`). Errors in the verdict file
  still fail on their own: a site with no row, a row that matches no site, a bad verdict or basis.
- **Hand edits and conflict markers.** Comparing verdicts alone would not close this: a marker in the summary touches no
  verdict. The lines outside the rows close it. A hand edit of a row, the header or a summary count, and markers outside
  the rows, each exit 1 (`hand-edit-*`, `conflict-markers-*`). On the real list: `changed text: listed '<<<<<<< HEAD'`.
- **What `--check` still ignores.** Line numbers, and the order of rows and of sites in a row that follows from them. So a
  wrong line number typed by hand passes with the note (`hand-edit-line-numbers`). A site that replaces another of the
  same pattern in the same function passes without one (`known-gap-swap`).
- **Two branches that each accept a site can still leave `coop` red.** Two green branches, one new `sim` site of
  `gPlayer[0]` each, in `fox_360.c` and `fox_effect.c`, merged with no conflict. Both had written `1789 sites`, so `--check`
  on the merge exits 1 with `changed text: listed '1789 sites in 407 functions in 43 files.', now '1790 sites ...`, until
  a plain rerun. With only the sites compared, the same merge passed with the note. Moved lines cannot do this.
- **A tracked file that is deleted but not committed.** `FileNotFoundError` before, from `--check`, `--todo` and the plain
  rerun. Now it is skipped. If the list has sites of it, a warning names the file and the sites count as lost; if not,
  nothing is said. A plain rerun then writes the list without them. Restoring the file afterwards shows them as new, so
  they need `--accept` (`file-deleted`).
- **A list with conflict markers.** In the conflicted merge above both sides' rows counted, so `--check` said `lost site`
  for each doubled function (`6 listed, 3 now`) and failed. A plain rerun wrote the list and passed.

Derived:

- The line in the #75 entry that any change moving a site needs a rerun no longer holds.
- Two branches merge cleanly into a stale list only when both shift the same rows alike, so both edits sit in one gap
  between sites of a file. Otherwise the list conflicts. A branch that only moves lines can now leave the list alone, and
  then it has nothing to conflict with.
- Two branches that change verdicts can leave `coop` red the same way, if their list changes merge cleanly. Not run.

Unknown:

- How far the line numbers will drift in practice. Nothing reruns the audit on its own.

### 2026-11-06, #73, a new site under an existing audit row

Read at `95962d04`. Python only: no build, no ROM, no game run. The tool behaviour below comes from temporary sites in
a worktree, reverted afterwards, and from `python tools/coop/sp_audit_test.py`.

Observed:

- **Before.** `--check` printed the same `is stale` line for a new site, a moved line and a removed site, and passed
  after a rerun, for all four kinds of new site tried: a new function under a `*` row (`fox_demo.c`) and under the
  `ovl_i*` glob row (`fox_co.c`), a new pattern and one more of a pattern in a function with a row
  (`Player_CheckBounds360`). An untracked file gave no output at all.
- **After.** The committed `single-player.md` is the baseline. A site it does not have, counted per path, function and
  pattern, is named with the row it would take its verdict from. A plain rerun then writes nothing and exits 1,
  `--check` stays red, and only `--accept` turns it green. Moved lines, a removed site and a changed row still need
  only the plain rerun. An untracked file with a site gets a warning and keeps the exit code.
- **One commit is enough.** The list is in the commit, and `git clone --depth 1` gave the same results: green as
  checked out, exit 1 with a new site. The scenarios of `sp_audit_test.py` run in a repository with no commit at all.
  CI's checkouts set no `fetch-depth` (`.github/workflows/coop-build.yml:68`, `:93`).
- **What it adds to the files: nothing.** The verdict file is unchanged and the list only changed its header. A count
  per row would have rewritten all 254 rows of the verdict file. With a site added under a row, `--accept` changed 0
  lines of the verdict file. All 36 overlay `.c` files changed at once took one `--accept` as well; 4 of those new
  functions had no row, so `--check` stayed red on those alone, as it did before this change.
- **Merges, tried in a scratch clone.** A branch that added a site with the old tool merged cleanly into this one, and
  `--check` passed. Two branches that each accepted a site in one function conflicted in the list only: a plain rerun on
  the file with its markers wrote it and passed, because both sides' rows count. Taking one side instead showed the
  other side's site as new, and one `--accept` cleared it.

Derived:

- `--check` still passes once someone has run `--accept`. It stops the author, not the reviewer. A reviewer can list
  what a pull request took in by putting the base's `single-player.md` in place and running `--check`.
- `--accept` says a site was shown, not that its verdict fits.
- A site that replaces another of the same pattern in the same function does not show: the list holds counts and line
  numbers, not the code on the line. Test `known-gap-swap` pins this.

### 2026-11-01, project setup

Observed: see DESIGN.md section 2 (player array, `gCamCount`, object arrays, RNG, event hooks, `USE_NETWORKING`).

Unknown: everything listed in DESIGN.md section 2, "Unknown".

### 2026-11-06, #68, controller state: devices, ports and what WP-14 reads

Base build of `origin/coop` at 95962d04: ROM US 1.0, Linux, GCC, Debug, OpenGL on a software renderer, no window, with
`libultraship` at eaaf9d0f. The runs were gdb on the headless game with a generated replay; `f357` is the 357th
`Game_Update` call of that run. Replay frame 0 fell between f20 and f35, because the replay starts after the boot logo
(`src/port/coop/test/replay.c:225-235`). No real pad was attached. The runs with pads used SDL virtual gamepads,
attached from gdb inside the game process (`SDL_JoystickAttachVirtual`, `SDL_JoystickSetVirtualButton`). Paths starting
with `controldeck/`, `controldevice/` or `physicaldevice/` are under `libultraship/src/controller/`.

**For WP-14: the local controller is port 0.** Read `gControllerHold[0]` once per frame, first thing in
`GamePreUpdateEvent`, on every client, whatever `CoopPlayers_LocalIndex()` says. `gControllerPlugged` and
`gMainController` are constants here and say nothing about devices. Make button press edges from your own copy of the
last hold, not from `gControllerPress[0].button`. No code was changed; the reasons are under Decision.

Observed, devices and ports:

- **Four ports always exist.** `LUS::ControlDeck` builds four `ControlPort`s with a `Controller` each
  (`controldeck/ControlDeck.cpp:126-128`). Nothing calls `ControlPort::Connect` or `Disconnect`
  (`controldeck/ControlPort.cpp:14-20`), so `GetConnectedController()` never says "nothing there".
- **Devices are not assigned to ports. A port has mappings, and a mapping names a kind of input.** A gamepad button
  mapping asks every gamepad its port does not ignore and sets the bit if any has it down
  (`controldevice/controller/mapping/sdl/SDLButtonToButtonMapping.cpp:21-28`). Keyboard events go to all four ports
  (`controldeck/ControlDeck.cpp:44-50`).
- **Every gamepad lands on port 0.** `RefreshConnectedSDLGamepads` puts each one on the ignore list of ports 1-3 and
  never on port 0's (`physicaldevice/ConnectedPhysicalDeviceManager.cpp:68-70`). Run: with one and with two virtual
  pads, each pad read ignored `[0, 1, 1, 1]` for ports 0-3, and A on either pad showed in `sNextController[0]` only
  (f356-360, f416-420).
- **A fresh config maps port 0 only.** `ControlDeck::Init` adds the defaults to port 0 and nothing to the others
  (`controldeck/ControlDeck.cpp:34-39`). Run: keyboard, mouse and gamepad mappings were `[1, 0, 1]` on port 0 and
  `[0, 0, 0]` on ports 1-3; the written config has `Port1` (port index 0) and no `Port2` to `Port4`.
- **A pad reaches port 1 only with a mapping there and off that port's ignore list.** Run, on port 1: mappings alone, A
  stayed on port 0 (f446-450). Off the ignore list too, the same pad drove ports 0 and 1 (f476-480). Ignored by port 0,
  it drove port 1 only (f506-510). The checkbox in the Input Editor makes those calls
  (`libultraship/src/window/gui/InputEditorWindow.cpp:1200-1207`); here gdb made them, and the editor was not opened.
- **Any hotplug undoes that.** A third pad arrived and the refresh put the second pad back on port 1's ignore list. It
  was still on port 0's, so it read `[1, 1, 1, 1]` and reached no port (f536-540). The lists live in memory only
  (`physicaldevice/ConnectedPhysicalDeviceManager.h:30`).
- **Who says what is connected.** C++ only, through `Ship::Context::GetInstance()->GetControlDeck()`:
  `GetConnectedPhysicalDeviceManager()->GetConnectedSDLGamepadNames()` lists all gamepads and
  `GetConnectedSDLGamepadsForPort(port)` those a port listens to; `GetControllerByPort(port)` gives
  `HasMappingsForPhysicalDeviceType(type)` and `HasConfig()`. Nothing says whether a keyboard or mouse is there; they
  only have mappings. From C there is only `ControllerBlockGameInput` and `ControllerUnblockGameInput`
  (`libultraship/src/public/bridge/controllerbridge.h:9-10`).
- **`osContInit` does not say either.** It sets the status of the first entry and bit 0 of the mask, whatever is
  connected (`libultraship/src/public/libultra/os.cpp:15-16`, `controldeck/ControlDeck.cpp:26`). Run: mask 1,
  `sControllerStatus` status 1, 0, 0, 0, so `gControllerPlugged` is (1, 0, 0, 0) until the first
  `Controller_UpdateInput`.
- **A port with no device reports a zeroed `OSContPad` with `err_no` 0, the same as an idle pad.**
  `osContGetReadData` clears all four (`libultraship/src/public/libultra/os.cpp:42`) and nothing in libultraship sets
  `err_no`. Run with `SDL_NumJoysticks` 0: over 3400 frames `err_no` was 0 on all ports and ports 1-3 held only zeros.

Observed, the derived points in #68:

- `gControllerPlugged` was (1, 1, 1, 1) on 3399 of 3400 frames, boot into Corneria. The other frame is the first
  `Game_Update` (`src/sys/sys_main.c:219`), which runs before `Controller_Init` (`:366`) and read (0, 0, 0, 0).
- `gMainController` was 0 on all 3400 frames and `Title_CheckControllers` returned 0 on all 124 calls. In the level
  `gInputHold` and `gInputPress` pointed at slot 0.
- Neither follows devices. `gControllerPlugged` stayed (1, 1, 1, 1) with 0, 1, 2 and 3 virtual pads, with a pad driving
  port 1 only (f507-510), and after all were removed, when `gMainController` was still 0.

Observed, reads:

- `gControllerPlugged` is read in `Controller_UpdateInput` and `Controller_Rumble` (`src/sys/sys_joybus.c:70`, `:111`),
  in `Title_CheckControllers` (`src/overlays/ovl_menu/fox_title.c:392-393`), and otherwise only for versus. In
  `src/overlays/ovl_menu/fox_option.c`: `Option_VersusMenu_Setup` (`:2668-2672`, an unplugged port is inactive and
  counts as confirmed), `Option_VersusMenu_Draw` (`:2746-2747`), `Option_Vs_Point_Selection` (`:3013-3014`),
  `Option_VsTimeSelection` (`:3053-3054`), `Option_VsStageSelection` (`:3095-3096`), `Option_VsConfirmSelection`
  (`:3149-3150`). In `src/engine/fox_versus.c`: `Versus_CheckForWinner` (`:838-887`, six reads),
  `Versus_GetMenuInput` (`:1029`), `Versus_UpdateMenu` (`:1138`), `func_versus_800C0E78` (`:1213`). Written at
  `src/sys/sys_joybus.c:57`, `:69` and `src/sys/sys_fault.c:242`.
- `gMainController` has 166 lines in `src` and `include`. Only two read its value: `Title_Setup` skips the attract
  sequence at -1 (`fox_title.c:204`) and `Title_PressStart_Draw` shows "No Controller" below 0 (`fox_title.c:2983`).
  The rest are its two writes, its two declarations and 160 uses as an index: 100 pad reads, 9 cursor arguments, 30
  rumble flags and timers, 21 rumble strengths. `docs/coop/audit/single-player.md` has the sites.
- With no plugged port the title locks up. gdb set `gControllerPlugged` to 0 at each `Game_Update`, so only game logic
  saw it. `gMainController` was -1 from f39, the attract sequence was skipped, and "No Controller" was drawn 832 times
  and "Press Start" never. The replay's A every 30 frames did nothing and the game was still on the title at f900,
  because the title then reads `gControllerPress[-1]`, memory before the array. Unforced, the same replay left the
  title 125 frames after reaching it.
- **Versus cannot be entered.** `DEBUG_VERSUS` is 0 (`include/mods.h:100`, upstream commit 18631e72, "disable versus
  mode for now"), so A on the VS card does nothing (`fox_option.c:975-979`). Run: with the cursor on card 2, A left the
  menu in the same state. `GSTATE_VS_INIT` has one writer, behind those screens (`fox_option.c:2993`). So every versus
  read above is dead code in a stock build.
- **Past that gate, the handicap screen needs all four ports.** gdb set `sMainMenuState = 11`, which is what the A press
  does after the gate. All four ports counted as present (`gPlayerInactive` all 0). A on port 0 set `D_menu_801B93C4` to
  1 and 13 more presses changed nothing. Only A on ports 1, 2 and 3 from the replay took it to 0xF (f830-890), and then
  port 0 could go on to the stage screen (f930).
- `Controller_Rumble` serves only entries with the status bit (`src/sys/sys_joybus.c:112`) and clears every flag
  (`:137`). Run: `gControllerRumbleEnabled` was 1, 0, 0, 0 in the level.

Observed, what WP-14 can trust in slot 0. Anything that writes a slot during the frame reaches
`Controller_UpdateInput` on the next one, because it keeps `gControllerHold[i]` as the previous state
(`src/sys/sys_joybus.c:71-74`). The replay writes all four slots (`src/port/coop/test/replay.c:277-278`). One virtual
pad held R for five frames in three cases, read at `Game_Update` before the replay wrote:

| Slot 0 meanwhile | `gControllerHold[0]` | `gControllerPress[0].button` |
|---|---|---|
| Nobody writes it, replay over (f522-525) | R | R on the first frame, then 0: correct |
| Replay writes it idle (f337-340) | R | R on every frame: the press repeats |
| Replay writes it with R held (f417-420) | R | 0 on every frame: the press is lost |

`gControllerHold[0]` was right in all three. A press took two frames to reach it: `sNextController` the next frame, hold
the frame after (f355, f356, f357).

Decision: no change to `src/sys/sys_joybus.c`.

- Co-op gains nothing. Outside `sys_joybus.c`, the campaign reads `gControllerPlugged` only in
  `Title_CheckControllers`, and WP-14 needs no answer from it: the local player is port 0 and everyone else comes from
  a stream.
- Port 0 has to stay plugged. A keyboard is not in any device list, so an answer from the gamepad list would show "No
  Controller" to keyboard players. See the forced run above.
- Ports 1-3 only matter to versus, which cannot be entered, and changing them with co-op off would leave upstream
  behaviour. A real answer also needs a C++ bridge, not a line in `sys_joybus.c`.

Derived:

- On a client, slot 0 will carry player 0's stream if WP-14 fills slots by player number, so the local pad has to be
  copied out before that write, and `gControllerPress[0].button` cannot be used after it. Sticks are safe:
  `Controller_AddDeadZone` rebuilds `gControllerPress[i].stick_x` and `stick_y` from the fresh hold
  (`src/sys/sys_joybus.c:13-49`, `:75`).
- A replay for the other players with a live local player needs the replay to stop writing the local slot.
- With the stock mappings ports 1-3 press nothing, so versus switched back on as it stands would stop at the handicap
  screen unless all four ports are mapped. That is the one thing a real `osContGetStatus` would fix, and it is
  upstream's: see `// LTODO: Fix this` (`src/sys/sys_joybus.c:62`), the same line in `origin/main`.
- Two pads on one PC drive one player, which fits one player per client. Split screen (WP-13, WP-15) needs a real
  assignment, and the ignore lists are not one: they are lost on hotplug and on restart.
- `Player_Update` picks input and rumble from `gMainController` outside versus (`src/engine/fox_play.c:5892-5894`), so
  WP-14 has to change that branch. If a client's local player is index 1, its rumble flag must end up at index 0, as
  only that entry is served.
- `gControllerLock` zeroes `sNextController` on all ports (`src/sys/sys_joybus.c:87-91`), so the local read is zero
  during a lock, as it is for the game today.

Not shown:

- A real pad: SDL's device drivers, real hotplug, real rumble. Virtual pads cover SDL's joystick and controller layers
  and everything above them, not what is below.
- Keyboard and mouse input in a run. Those lines are from reading.
- Windows, macOS, a pad attached before boot, and restarting with a saved port 1 mapping.

Unknown:

- Whether two game instances on one PC both follow one pad. `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` is on
  (`libultraship/src/window/gui/Gui.cpp:173`), which suggests they do; not run.

### 2026-11-06, #30, what a client shows and hears: its own player, and the other one

Identity: ROM US 1.0, branch `issue/30-local-player-view` on top of `2c63dacb` (PR #128), Linux, GCC, Debug, OpenGL on a
software renderer, no window, no transport. Two games per section, one replay, local index 0 and 1. Corneria only.

Method: `tools/coop/view_check.py`. Input by rule flies both players, and buttons and pokes give them different numbers,
so a client showing the wrong player shows the wrong number. A view trace (`src/port/coop/test/view_trace.h`) logs what
each client holds to show and every sound request. That is not the same as what reached the screen, so pictures were
read too: a shared library, not committed, read the OpenGL buffer of a game with no window on chosen frames, and every
picture was deleted after reading. It needs `glReadBuffer` left alone (a pbuffer has no back buffer) and a frame test
with an upper bound, as `gGameFrameCount` carries over from the menu until the level resets it. Both cost a run of black
pictures that said nothing about the game.

Observed (runs), before, test commit: `camera-own` and `hash` passed. Everything else failed:

| Rule | Before | After |
|---|---|---|
| `camera-parked` | 0 of 1674 parked frames look through player 0 | all |
| `hud-shield` | client 1 shows 0.784, its player has 0.471 | all frames right |
| `hud-bombs` | client 1 shows 5, its player has 7 | all frames right |
| `cockpit` | glass on the other client on 95 and 89 frames | 0 and 0 |
| `flash` | a hit reddens both clients, 16 frames each | only the client of the player hit |
| `shield-bar` | no bar | the other player's, right value, never the own one, none while parked |
| `sound-own` | view change and shield alarm on both clients | only the client of that player |
| `sound-engine` | player 1 has no engine | starts on every join, off while parked |
| `sound-source` | own player ahead of the camera on 605 of 1226 frames | all |
| `player-down` | not in the test commit | client 1 looks through player 0, engine off |

`hud-bombs` allows the counter's own step, a frame or so behind the player's number. In all-range `shield-bar` is exact:
the bar shows on every frame the other player is ahead of the camera and on none it is behind (0 wrong of 1074 and 1036
frames), on rails at least half the frames, as the trace has no `zPath`. `pause`: player 0's pad paused both clients for
the same 30 rows and player 1's pad paused neither, and `hash` stayed equal through pokes, cockpit view and pause.

Observed (runs), the rest, after:

- `hash_check.py views`: equal on all 10500 rails and 8000 all-range frames. The cameras differ on 56 of 74 and 31 of 49
  sampled frames, against 74 of 74 and 49 of 49 in #96: on the others a client now watches player 0 instead of an empty
  camera, so equal is right there.
- Co-op off, one player, the test commit against this branch, with a hit, low shields, cockpit view and pause: state
  hash and whole view trace equal, 5734 frames and 2633 rows on rails, 6309 and 3208 in all-range. That holds for the
  camera, the fill and every sound request. The red fill of a hit is there in both, 48 and 80 rows, so the old path
  still runs.

- With #12: a trial merge of `origin/issue/12-input-source` at `231e54e1`, not kept, built and passed `view_check.py`,
  hash and pause included. The replay flies every player there, so the local controller's own path did not run.

Observed (pictures), after, both clients on the same frame:

- Before player 1 joins both show the same intro. Before the fix client 1 showed grey polygons, from an empty camera.
- Lives, bombs and the shield and boost gauges differ per client. Lives and boost have no value a trace can read, so
  this is their only check: poked to 4 and 7, each client showed its own.
- The other Arwing is whole, in its own pose, with a green bar above it and the empty part dark. In cockpit view a
  client sees its cockpit, and the other client sees that player as an Arwing, no glass.
- A hit reddens the screen of the player hit only, either way round.
- All-range: ground, backdrop and radar follow the client. The radar marks both players.

What changed, and where it writes:

| Item | Change | Touches the simulation |
|---|---|---|
| Camera | `CoopPlayers_ViewIndex`: the local player while it flies, player 0 while it is parked | No |
| Who a client is | `CoopPlayers_LocalIndex` stays the own player, parked or not | No |
| HUD numbers | `CoopPlayers_HudIndex` for what is only shown; `gPlayerNum` stays 0 for the HUD | No |
| `gBombCount` limit (`src/engine/fox_hud.c`, `HUD_BombCounter_Draw`) | Goes through every player | The same on every client |
| `gLifeCount` (level clear, pause: `HUD_LevelClearStatusScreen_Draw`, `HUD_PauseScreen_Update`) | Stays on slot 0: `gPlayerNum` is 0 there | Unchanged |
| Cockpit (`Display_Arwing`, `Display_CockpitGlass`) | Only the player looked through | No |
| Hit flash (`Player_Update`) | Left out of `gFillScreen*` in co-op, drawn per client from `dmgEffectTimer` | Same on every client: the fade no longer moves |
| Boss shadow (`Object_DrawAll`) | By the camera drawn; `D_edisplay_801615D0` stays player 0's for the AI | No |
| Glare, lock-on mark, help arrow, starfield roll, backdrop, ground | The camera drawn | No |
| Other Arwing (`Display_ArwingWings`) | Its own `jointTable`, wing flash and `dmgEffect`, not player 0's; every pilot shows | `drawFace`, the same on every client |
| Shield bar (`src/port/coop/game/view.c`) | Quad from `starDL`'s pattern, no asset | No |
| Radar | Players 1 and up in `gRadarMarks[60..62]`, slots 1 to 3 being the wingmen's | Hashed, the same on every client |
| Engine sound | Every joined player, started on a join, stopped on leaving | No |
| Sound position (`Object_SetSfxSourceToPos`) | From the camera drawn; leaves `gCalcMatrix` alone, as simulation code calls it | No |
| Alarms and pickups (`Player_ApplyDamage`, `Object_PlayerSfx`, view change) | Only the player looked through | No |

Audit rows marked `view`: six have no site left. The 55 others stay, for one of these reasons:

- **Level state, the same for everyone:** `gPlayer[0].state` and `csState`, as in `Radio_Draw`, `Background_DrawSun`,
  `Object_DrawAll`. Player 0 drives the level, and a client whose player is parked looks through it.
- **Writes a hashed value, so it must stay on player 0:** billboards in update code (`Sprite_UpdateDoodad`,
  `MeteoBall_Update`, `Effect_Effect362_Update` and others) and `Object_SetMatrix`, which writes `obj.rot` of buildings.
  These face player 0's camera on every client. Picture only, and small while the players fly near each other.
- **Levels nothing has run:** the overlays' `name` rows, the tank, Aquas.

Derived:

- A client's identity and its view are two things. Input (#12) needs the first even while parked: on that branch
  `CoopInput_Route` hands the local controller to slot `CoopPlayers_LocalIndex()`, so a fallback to 0 there would steer
  player 0 from client 1, and the clients would disagree on player 0's input. `view_check.py` `local-index` guards it.
- Pause stays in the simulation and every client agrees: `gPlayState` follows `gControllerPress[gMainController]`, and
  `HUD_PauseScreen_Update` reads `gInputPress`, which after the player loop is player 0's. On #12's branch slot 0 holds
  player 0's input on every client (`input.h`), so that holds after merging, as long as `gPlayerNum` stays 0 there, as
  #96 set it (`src/engine/fox_play.c`, after both player loops).
- With #12 merged, rumble reaches the local pad for the client's own player: `CoopInput_RumbleSlot` gives it entry 0,
  the only one `Controller_Rumble` serves. The direct writes to `[gMainController]` and `[0]` are level events, and they
  reach entry 0 on every client. Read, not run. Without #12, player 1's rumble goes to entry 1 and no pad.

Choices this makes, all reversible, none a game rule with a record (`DESIGN.md` section 4):

- A client with a parked or downed player watches player 0, HUD and all.
- Every client hears every engine, placed from its own camera, rather than its own engine only.
- Lives shown are `gLifeCount[<player looked through>]`. Whether lives are shared is open (section 4, item 2).
- Only player 0 can pause, as only its input reaches `gControllerPress[gMainController]`.
- On the radar a wingman sharing a colour with the local player blinks with it, until WP-17 replaces that wingman.

Unknown:

- Whether any of it feels right: nobody has flown it, looked at it moving or heard it. Sound is checked as requests and
  positions, not by ear. The shield bar's size and height are a first guess.
- The boss shadow: no run had the two views disagree on a boss, so that fix is read, not run.
- Every level but Corneria, local indexes 2 and 3, a real session: only the test switch sets the local index so far. The
  session hands out `playerIndex` (`session.h`), and the join flow (WP-24) has to pass it on.
- Player 1's own lock-on mark: not shown. Slot 0 belongs to player 0, and the draw code only fills that slot on rails.

### 2026-11-05, #8 (WP-10), player count apart from camera count

Observed:

- `gCamCount` means two things. It is the number of players and cameras, and in about 25 places it is also the
  versus flag: `gCamCount != 1` or `gCamCount == 4` turns on player-vs-player hits, the versus HUD, no radio, no
  positional sound and arena wrapping (list in `docs/coop/audit/sp-verdicts.tsv` from WP-05, rows with `gCamCount`).
  So co-op cannot just set `gCamCount = 2`.
- Outside versus, every player reads the main controller: `Player_Update` sets `gInputPress` from
  `gControllerPress[gMainController]` unless `gVersusMode` (`src/engine/fox_play.c:5884-5891`). A second player
  copies the first player's input until WP-14 gives it its own input source.
- `Play_Update` leaves `gPlayerNum` at the last player after its update loops (`src/engine/fox_play.c:7036-7053`).
  In versus that is fine; with more players than cameras, code after the loop would act for the last player.
- The per-player arrays have 4 slots (`gPlayerGlareAlphas[4]`, `gPlayerScores[4]`, `gControllerRumbleTimers[4]`,
  `gVsLockOnTimers[4][4]`, `include/sf64context.h:124-293`), so the player count is capped at 4.

What changed (`src/port/coop/game/players.c`, every game-side site marked `// COOP:`):

- `CoopPlayers_Count()` is the player count. With `gCamCount != 1` (versus) it returns `gCamCount`; in the campaign
  it returns the count fixed by `CoopPlayers_LevelInit()` in `Play_Init`. Until the session exists (WP-21, WP-24)
  that count comes from the debug CVar `gCoopDebugPlayerCount` (default 1, clamped to 1..4). `gCamCount` stays 1.
- Switched to the player count, because they run once per player: allocation and init in `Play_Init`
  (`fox_play.c:2950-2953`), `Player_InitializeAll` (`fox_play.c:2610`), the "last player set up" check in
  `Player_Setup` (`fox_play.c:4933`; back on `gCamCount` since WP-11), the update and camera loops in `Play_Update`
  (`fox_play.c:7036, 7048`), the sound cleanup when a level is quit (`fox_play.c:7188`), the five player draw loops in `Display_Update`
  (`fox_display.c:1969-2068`), the engine noise loop (`audio_general.c:2137`) and the object RAM viewer
  (`src/mods/object_ram.c:323`).
- After each loop in `Play_Update`, with co-op on, `gPlayerNum` is set back to `CoopPlayers_LocalIndex()` (0 for
  now; WP-28 makes it per client).
- Left on `gCamCount` on purpose: every versus check, the loops inside `if (gVersusMode)`, viewports
  (`fox_game.c`), lock-on indicators and target positions (`Display_LockOnIndicator`, `fox_edisplay.c:1272`), radar
  marks (`HUD_RadarMarks_Setup`) and player-shot hits on players (`PlayerShot_CollisionCheck`, `fox_beam.c:837`).
  Those are view (WP-28) or game rules (friendly fire, WP-07 and WP-16).

Tested (Windows 11 Home 10.0.26200, MSVC 14.44.35207, Debug, US 1.0 ROM, default graphics backend from a fresh
config; work on top of `coop` 1ec6c677, merged locally with the WP-03 replay code 070256bd for the test only):

- `gCoopDebugPlayerCount` unset (co-op off): `tools/coop/replay.py play` of a recorded Corneria run (13545 frames,
  recorded on the pre-WP-10 build) gave PASS, `first_mismatch=-1`, level complete at frame 11747, same as the
  recording.
- `gCoopDebugPlayerCount = 2`, same replay: the game kept running for all 13545 frames and after, no crash. First
  checkpoint mismatch at frame 4926, where only the RNG seeds differed and player 1's position still matched. The
  level was not completed; the run was stopped after 600 s.

Derived:

- The second player's update draws random numbers, so the RNG drifts as soon as it is in the level. That is
  expected and why the mismatch shows up in the seeds first.

Unknown:

- What player 2 does in Corneria with this change: where it spawns, whether it is drawn where expected, why the
  run with 2 players did not reach level complete. That is WP-11 (all-range) and WP-12 (on-rails).
- Whether `Camera_Update` for player 2 writes globals that the local camera reads (WP-28).
- `gCoopDebugPlayerCount` changes the simulation, so it belongs in the sim CVar list of WP-21 once both are merged.
  (Done in #55.)

Found later (2026-11-06, while testing WP-11 by watching a 2-player Corneria run):

- Over water the graphics broke, and the run later ended in an MSVC debug assertion (`back() called on empty
  vector`, in `<vector>`). Cause: in `Display_Update` the loop that draws reflected player features over water pushes
  the matrix once before the loop but popped once per player inside it (`src/engine/fox_display.c`, the
  `gGroundSurface == SURFACE_WATER` block after `Effect_Draw`). With one player that balances; with two the matrix
  stack underflowed every frame over water. Versus never reaches it (no water). Fix: one pop after the loop.
  Verified: the owner watched a 2-player run over Corneria's water, graphics normal, no assertion; 1-player replay
  still PASS. That the assertion came from this underflow is a guess; it did not come back after the fix.

### 2026-11-06, #10 (WP-12), player 2 on rails: who owns the path

**Player 0 owns path progress.** The other players take the path from it every frame and write no path global
(`CoopPlayers_FollowPath`, `src/port/coop/game/players.c`, called at the top of `Player_UpdatePath`). This is the shared
path WP-06 proposed; DESIGN.md needs no change of plan. Why player 0:

- Everything that hangs off the path already reads player 0 or the globals `Player_UpdatePath` writes: object loading
  (`src/engine/fox_enmy.c:592-595`, `:630-633`), the x/y part of culling (`:2695-2697`), the path-turn items
  (`:2550-2591`). With player 0 as the only writer none of those sites changes.
- There is one world. Objects are drawn at `obj.pos.z + gPathProgress` and floor collision adds the same global, so two
  progress values cannot both be right in one simulation. A rule over all players (the one in front, an average) would
  jump when one of them goes down.
- Player 0 is always there: the host, always Fox (decision 0005). Nothing has to hand the path over when a client leaves.
- The price: only player 0's boost and brake change the speed of the level. The others still pull their own `camDist`
  (towards -400 and 180), so they move ahead of and behind player 0, and `trueZpos`, shots and hit tests move with them.
  How that feels: `unknown`, nobody has played it.

What changed:

- `CoopPlayers_SkipUpdate`: a waiting player joins when player 0 is `ACTIVE` on rails in an Arwing, as well as in
  all-range. The Landmaster and the Blue Marine stay parked on rails (WP-18).
- `CoopPlayers_FollowPath`: besides `zPath`, `xPath` and `yPath` a follower takes `pos.z`, `vel.z`, `baseSpeed`, the path
  turn state and the view turn (`yRot_114`, `xRot_120`), because `Player_MoveArwingOnRails` rotates the flight vector
  with them. The box sizes are copied once, at the join: the Landmaster uses `pathHeight` for its own jump
  (`src/engine/fox_tank.c:679`). No level or player count is named anywhere; players 1 to 3 run the same code.
- New slot `COOP_SLOT_AWAY`: a joined player whose section ends is parked, and comes back when player 0 flies again.
  WP-11 sent it `OUT` for the rest of the level, which would have ended it at the cut from rails to all-range. It keeps
  its own shields, wings, laser and bombs. `OUT` is now only for a player that went down.
- `CoopPlayers_SelectInput`: player 0 reads `gMainController`, the others the pad with their index, so a replay can fly
  player 2. Before, all of them read `gMainController` (`src/engine/fox_play.c:5892-5898`), so player 2 copied player
  0's stick. `Camera_UpdateArwingOnRails` reads `gInputPress` too (`:6228-6229`), so the camera loop selects it per
  player as well, and both loops put it back on the local player, as they do for `gPlayerNum`. With one controller
  player 2 now flies straight on instead of copying. WP-14 replaces this with a real input source.
- `src/port/coop/test/player_trace.c` logs the path globals, each player's path, `camDist`, shields, somersault and form.
  `tools/coop/player_trace.py rails` is the test below; its `check` rules now expect the join on rails.

Observed:

- `player_trace.py rails --players 2` (ROM US 1.0, Linux, GCC, Debug, OpenGL on a software renderer, no window; both
  runs with `gInvincible`; player 0 fires, boosts and brakes, player 2 sweeps the stick, fires, boosts, brakes and
  loops): the rail section is trace frames 1674 to 6198 in both runs, 4525 frames. `gPathProgress`, `gPathVelZ` and
  `gPathTexScroll` equal single player on all 4525, largest difference 0.000. Progress 0 to 184448, 40.771 per frame,
  speed between 20 and 70. Player 1 flew all 4525 frames on player 0's `zPath`, `xPath` and `yPath`, 2400 across in x,
  1146 in y, on average 814 from player 0 in x, `camDist` -323 to 115, 7 loops. It parked one frame after player 0 went
  to `START_360` and was active in all-range on frame 6441, the same frame as player 0.
- The same with 4 players, all three extra players: path equal on all 4525 frames, all 60 rules pass. (On the first run
  the box rule failed for players 1 and 2, and every frame it named was inside a loop, see below. The rule leaves loops
  out now, and it passes on the same traces.)
- With `CoopPlayers_FollowPath` returning 0 (a temporary build) the same test fails as WP-06 predicted. `gPathProgress`
  matches single player on 26 of 4525 frames, largest difference 40889.953, 31.733 per frame instead of 40.771, the rail
  section ends on frame 7414 instead of 6198, and `gPathTexScroll` is off by 143559, because both players add to it.
- With extra players reading pad 0 and `AWAY` replaced by `OUT` (a temporary build): `looped 0 times` and `active in
  all-range on frame None`.
- Player 0's own x and y are not the same as in single player: up to 542.8 apart with 2 players, 418.8 with 4. Player 2
  changes the RNG and what is left of the enemies, so player 0 is hit in other places. Derived: under `gInvincible` that
  moves it without changing its speed; without it, a hit can cost player 0 boost on a different frame than in single
  player, so an exact match is only to be expected in this test.
- Level complete on rails (`gDebugLevelComplete`, L at replay frame 6000): `check` PASS, player 1 parked one frame after
  player 0, at trace frame 2901.
- Hash, two players, `hash_check.py run <rail replay> --players 2 --poke-frame 5400 --poke-player 1`: A and B equal on
  all 10500 frames, the poke shows at exactly frame 5400, in `players`. These runs have no `gInvincible`, so they hold a
  player 0 going down and a restart as well.
- Co-op off: one plain 1-player run of `95962d04` and of this branch on the same replay, `hash_check.py diff`: 10512
  common rows, no difference.
- Player 2 going down on rails (no `gInvincible`, gdb sets `shields = 0` and `radioDamageTimer = 5` at trace frame 2500):
  `check` PASS, out on that frame, drawn for that one frame, and the path went on with player 0 alone, 33754 to 47047
  until player 0 went down at frame 2808. In the two restarts of the same run player 1 went down by itself from
  collisions (frames 1545 and 1645), and player 0 flew on for 140 and 371 frames.
- Player 0 going down on rails: player 1 parked, level restart, parked again, and back on frame 1 with full shields,
  because a restart without a checkpoint is `ACTIVE` from frame 1. It is hidden on that frame: the copy takes player 0's
  `draw`, which is still 0 there.
- Player 2 is in player 0's view (gdb, frames 2300 and 2301): view space (-604, 225, -384) with player 0 at (47, 104,
  -393), so beside it at nearly the same depth, and `sPlayersVisible[1]` is 1. `sPlayersVisible` of the local player
  reads 0 between frames because `Display_Update` clears it at its end (`src/engine/fox_display.c:2103`, also upstream).
- A path turn, forced because this route has none (gdb on player 0 at frame 2000: `xPathTarget` +1200, `pathChangeTimer`
  60, `pathChangeYaw` -30): `xPath` went 0 to 1200 on both players, and across all three attempts of that run player 1
  was off player 0's path on 0 of 4014 flying frames.
- The cut from rails to all-range keeps what is the player's own (gdb at frame 6100: shields 100, bombs 7): after the
  rejoin at 6441 it has shields 100, bombs 7 and its two broken wings, with player 0 at 3 bombs. Laser strength: not
  shown. It was back to single on the next frame, because `Player_Shoot` does that to broken wings
  (`src/engine/fox_play.c:3466-3469`). The rejoin does not touch that array, but this run does not prove it.

- `Player_PerformLoop` moves `pos.z` like normal flight (`src/engine/fox_play.c:3983`), so a follower's loop has no z
  travel: it goes up and comes down. A loop has a floor but no sides or top (`:3977-3982`; the box is in
  `Player_MoveArwingOnRails`, `:4154-4173`), so any player can leave the box during one: up to 211 past the side in the
  4-player run. The rail test leaves those frames out.
- Stick up is nose down: `stick_y` above 0 takes an Arwing to the floor of the box (seen in the first test run).
- Flying straight on with no stick input and A pressed, an Arwing is shot down at trace frame 3365, z -68228, about a
  third of the way through the section. So the rail test sets `gInvincible` in both runs. Under it `damage` is 0, so the
  boost penalty in `Player_UpdateEffects` (`dmgType >= 40`, `src/engine/fox_play.c:5600-5606`) stays off, which is why
  path speed can match exactly while player 0 is hit in other places.
- This route has no path turn: `xPath` and `yPath` stayed 0 for the whole section.
- **Any player can collect a path item.** `Player_CheckItemCollect` sets `item->collected` for whoever flies through
  (`src/engine/fox_play.c:1662-1669`), and `ItemPathChange_Update` then turns player 0's path
  (`src/engine/fox_enmy.c:2547-2591`), which the others follow. That fits DESIGN.md section 4 item 4 ("whoever triggers
  it; the whole team follows"), except that a split picks its side from player 0's position, not the collector's
  (`:2555`, `:2575`). For WP-16.
- There is no friendly fire on rails: shots are tested against players only in all-range (`src/engine/fox_beam.c:836`).
- Enemy shots only test player 0: `func_effect_8007A774(gPlayer, ...)` (`src/engine/fox_effect.c:1396`, `:3015`,
  `:3117`, `:3916`) and `Player_ApplyDamage(gPlayer, ...)` (`:2275`, `:2946`, `:3080`, `:3889`). A follower is hit by
  what `Player_CollisionCheck` walks: scenery, actors, bosses. Those sites settle part of the WP-11 unknown.
- Items credit the player who took them (`this->playerNum`, `src/engine/fox_enmy.c:2223-2340`). Kills do not:
  `actor->dmgSource = shot->sourceId + 1` (`src/engine/fox_beam.c:696`), and only `AI360_FOX + 1` adds to `gHitCount`
  (`src/engine/fox_enmy.c:1761`). For WP-16 (DESIGN.md section 4 item 5).
- `gTeamLowHealthMsgTimer` counts once per player per frame (`src/engine/fox_play.c:5842`), so that radio timer runs
  twice as fast with two players. For WP-17 and #60.

For WP-28 (nothing changed there):

- `Camera_Update` already runs for every joined player, so `gPlayer[1].cam` is kept up to date on rails, and the view in
  `Display_Update` is built from `gPlayer[gPlayerNum]` with path fields that now match player 0's. That looking through
  player 1 then just works is a guess; nothing has rendered it.
- A player in cockpit view (C-up) draws the cockpit model and sets `sDrawCockpit` without checking it is the local
  player (`src/engine/fox_display.c:755-758`), so player 2 in cockpit view would put the cockpit glass on player 0's
  screen. Read, not run: the test input does not press C-up.
- A hit on any player flashes the whole screen red (`gFillScreenAlpha`, `src/engine/fox_play.c:5562-5569`). The view
  change sound plays for any player (`:5969-5971`), and every flying player writes `gShowHud` (`:5961`).

Unknown:

- Other rail levels (Meteo, Sector X and Y, Solar, Zoness, Area 6, Venom): players now join there too, not run. A warp
  (`PLAYERSTATE_ENTER_WARP_ZONE`) and Corneria's other route are not run either.
- Whether two Arwings look right on rails: nobody has watched it. The trace and the view positions above are all that
  was read.
- Boost and brake for player 2 only as `camDist`: see above.

### 2026-11-05, #9 (WP-11), player 2 in Corneria's all-range section

Observed:

- Corneria's only all-range section is the Granga fight on the normal route. `Corneria_Granga_Init` switches player 0
  alone: `gPlayer[0].state = PLAYERSTATE_START_360` (`src/overlays/ovl_i1/fox_co.c:102-110`). The cutscene
  `Cutscene_AllRangeMode` sets `gChangeTo360` on its frame 37 (`src/engine/fox_demo.c:771-788`), and `Play_Main`
  then sets `gLevelMode = LEVELMODE_ALL_RANGE` (`src/engine/fox_play.c`, `if (gChangeTo360)`).
- In the recorded Corneria run (`local/replays/corneria.sfcr`, normal route), the trace shows: rails until frame
  6454, `START_360` from 6454, mode all-range from 6493, player 0 `ACTIVE` from 6696, `LEVEL_COMPLETE` at 8621
  (trace frames are `gGameFrameCount`, not replay frames).
- `Player_Setup` resets level-wide state besides the player: radio (`gRadioState`, `gRadioMsgRadioId`), the start
  screen timer, the intro flag `D_ctx_8017782C`, the music, `gPlayCamAt`/`gPlayCamEye`
  (`src/engine/fox_play.c`, `Player_Setup`). Running it for a player who joins mid-level would disturb player 0.
- `PLAYERSTATE_STANDBY` is not a quiet state: each frame it sets `gShowHud = false` and `gPauseEnabled = false`
  (`src/engine/fox_play.c`, `Player_Update`, `case PLAYERSTATE_STANDBY`), so it cannot park a second player.
- `Player` holds two pointers, `sfx.srcPos` and `sfx.srcVel` (`include/sf64player.h:159-160`). A copy of player 0
  starts with them pointing into player 0; `Display_SetupPlayerSfxPos` (`src/engine/fox_display.c:1748`) points them
  back at the player's own `sfxSource` and `sfxVel` on every draw (`:1977`). `gPlayer` lives for the whole level, so
  copying one `Player` into another is safe.
- Shots from player 1 can hit player 0. The all-range hit loop walks `gCamCount` players and only skips the shot's
  own source (`src/engine/fox_beam.c:837`, `:855`), so a shot with `sourceId` 1 is tested against player 0, while
  player 0's shots are never tested against player 1. Friendly fire is a game rule (#7); this does not change it.
- `Player_Update` sets `draw = true` after `Player_Update360` (`src/engine/fox_play.c:5981`), so hiding a player from
  inside `Player_Down` lasts only until that line.
- The all-range camera code (`Camera_UpdateArwing360` to `Camera_Update360`) writes no globals. The starfield is set
  from the draw code (`Camera_SetStarfieldPos`, `src/engine/fox_display.c:1940`).

What changed (`src/port/coop/game/players.c`, two `// COOP:` checks in `Play_Update`):

- In co-op, players 1 and up start parked: allocated but not updated, no camera update, not drawn (their `draw` flag
  stays 0 from `Player_InitializeAll`).
- When the level is in all-range mode and player 0 is `ACTIVE`, each parked player joins as a copy of player 0,
  250 units to the side and 80 up per index, with the per-player arrays `Player_Setup` fills (wing health,
  timers, laser, bombs, lives) copied from index 0. It updates from the next frame on.
- `tools/coop/player_trace.py` and `src/port/coop/test/player_trace.c` (`SF64_COOP_TRACE`) log and check each
  player's state and position per frame.
- A joined player goes out (parked and hidden, `draw = 0`) as soon as player 0 is no longer `ACTIVE` or `U_TURN`
  (level complete, down), and `Player_Down` returns early for co-op players 2+ (one `// COOP:` check), so they go
  out instead. Out players stay out until the next `Play_Init`; respawn is a game rule (WP-16). `draw` is cleared
  again on every frame while out, so a player that goes down is drawn for that one frame only.
- The "last player set up" check in `Player_Setup` is back to upstream's `gCamCount` (`src/engine/fox_play.c:4933`).
  Only player 0 runs `Player_Setup` now, so with `CoopPlayers_Count()` the check never fired, `gClearPlayerInfo`
  stayed set, and every level start reset lives, bombs, laser and team shields: game over could not be reached.

Tested (Windows 11 Home 10.0.26200, MSVC 14.44.35207, Debug, US 1.0 ROM, default graphics backend from a fresh
config; this branch on top of WP-10 8b78fbf5, merged locally with the WP-03 replay code 070256bd for the test only):

- Co-op off (count 1): Corneria replay PASS, `first_mismatch=-1`, level complete at replay frame 11747.
- Count 2, same replay, `player_trace.py check`: PASS. Player 1 parked for all 6493 rail frames, active in all-range
  on frame 6696 (the same frame as player 0), 3819 active frames, 41.9 units per frame on average, distance to
  player 0 between 159 and 4347. It turned at the arena edge on its own (`U_TURN`, like player 0).
- In the same run the replay stops matching at replay frame 9887, player 0 goes down at trace frame 11145 and the
  level restarts from the checkpoint; player 1 is parked again after the restart.
- Watched by the owner, 2026-11-06 (no replay, owner playing, `gCoopDebugPlayerCount = 2`), first build: player 2
  appeared at Granga, but graphics broke over water (WP-10 bug, fixed there) and after Granga player 2 kept flying
  during player 0's level-complete cutscene, crashed at trace frame 9941, and `Player_Down` started the fade to a
  level restart (stopped music, "player down" theme) for everyone; the run then hit the debug assertion.
  `Player_Down` (`src/engine/fox_play.c`) stops the BGM and plays `NA_BGM_PLAYER_DOWN` for any player outside versus.
- Watched again with both fixes: graphics over water normal; player 2 looked normal at Granga and flew along; at
  level complete (trace frame 8279) player 2 went out on the same frame and the cutscene ran to the end (player 0
  `NEXT` at 9612, level left normally). No assertion. Owner's report: "Wasser ok, Spieler zwei hat normal ausgesehen
  und ist verschwunden am Schluss."
- Replay with the fixes: 1 player PASS (unchanged); 2 players `player_trace.py check` PASS; when player 0 went down
  (trace frame 11145) player 1 went out on the same frame.

Tested on Linux (GCC, Debug, OpenGL software rendered, no window, US 1.0 ROM, this branch on `coop`). This is not a
recording and not the normal route: a generated replay presses A every 30 frames until Corneria starts (replay frame
3101), then L with `gDebugJumpToAllRange` starts the all-range section.

- Count 2, `player_trace.py run`: PASS. Player 1 parked for all 1779 rail frames, active on frame 1982 (the same frame
  as player 0), 901 flying frames, 41.7 units per frame on average, never still.
- Player 1 forced down through gdb (`shields = 0`): `check` PASS, drawn for at most one frame after going out. With the
  `draw` line in `CoopPlayers_SkipUpdate` removed the check fails: drawn for 621 frames while parked.
- Level complete while player 1 flies (`gDebugLevelComplete`): `check` PASS, out one frame after player 0. With the
  leave check in `CoopPlayers_SkipUpdate` removed the check fails: flying on for 203 frames.
- `gClearPlayerInfo` read through gdb when player 1 joins: 0. With `CoopPlayers_Count()` back in `Player_Setup`: 1.
- Co-op off: a recording made before WP-10 (title and attract only, 4331 frames) PASS, no mismatch.
- `tools/coop/player_trace.py` against bad traces and a fake game (crash, hang, early exit, freeze, wrong player count,
  player flying on, hidden while flying, not parked after a restart): every case gives the expected exit code.

Not covered by these runs: a level restart, enemy contact with player 1, and Windows with these fixes (build only).

Derived:

- Player 1 copies player 0's controller (both read `gMainController`), but from another position, so it flies a
  different path. Its update draws random numbers, so the enemies act differently and the recorded inputs no longer
  fit. Expected until each player has its own input (WP-14) and the run is recorded with two players. That this is
  why player 0 goes down is a guess; shots from player 1 hitting it (above) could also play a part.

Unknown:

- The normal level-complete path is covered by the owner's watched run only; the replay diverges before it with 2
  players, and the Linux run above reaches it through `gDebugLevelComplete`.
- Whether enemies target or hit player 1: enemy and shot loops still use `gCamCount` or `gPlayer[0]`, so probably
  not (guess). Friendly fire and targeting are game rules (WP-07, WP-16).
- Player 1 has no engine noise: `Audio_StartPlayerNoise(gPlayerNum)` is called in `Player_Setup`
  (`src/engine/fox_play.c:4954`), which a joining player skips. Whether it should get one is for WP-28 (sound follows
  the local player).
- Other all-range levels (Fortuna, Katina, Bolse, Sector Z, Venom 2) start in all-range; there player 1 would join
  once player 0 is active after the intro. Not run.

## Determinism

### 2026-11-06, #96, draw code is part of the simulation: one camera per client

Identity: ROM US 1.0, branch `issue/96-draw-state` on top of `797c678b`, Linux, GCC, Debug, OpenGL on a software
renderer, no window, one game per run, no transport. Input by rule (`tools/coop/player_trace.py`, `write_rail_replay`),
2 players, `gInvincible`. Corneria only, on one machine.

Method, three ways, because each misses what the others see:

- **Run:** `hash_check.py views` plays one replay with `SF64_COOP_TEST_LOCAL_INDEX` 0 and 1, which is two clients of one
  session that draw different cameras.
- **Measured:** a throwaway build compared all of `.data` and `.bss` before `Game_Draw` and after `GamePostUpdateEvent`
  on every in-level frame, so it lists what the draw section really changes (4500 in-level frames, rails into all-range).
  Not committed. It shows net changes only: a write of the value already there does not appear.
- **Read:** every function reachable from the draw entry points in `Game_Update` and from every `draw` slot of
  `gObjectInfo`, following any function named in a reachable body, stopping at the `Ovl*_CallFunction` dispatchers. In
  level code that is 595 functions and about 2180 writes to something not a plain local. Then, for each global, whether
  anything outside that set mentions it, and for each camera-derived value who reads it.

Observed (runs), before the fix, test commit `e18af46e`: the logs split at once, and not only in what the camera touches.
First differing frame per region:

| Region | Rails, 10500 frames | All-range, 8000 frames |
|---|---|---|
| `players` | 3102 | 3102 |
| `actors` | 3103 | 3103 |
| `effects` | 3389 | 3389 |
| `shots` | 4801 | 4801 |
| `scenery` | 5371 | none |
| `items` | 5970 | 7719 |
| `bosses` | 8767 | 4880 |
| `misc` | 9582 | 5194 |
| `rng` | 9683 | 5188 |
| `sprites` | 10139 | 7052 |
| `globals` | none | 5586 |
| `scenery360` | none | none |

Frame 3102 is one frame after Corneria starts. `rng` differing means the two clients no longer play the same game, so
this is a real divergence, not only a hash that sees too much.

Observed (runs), after the fix, commit `66df1cac`, the same two replays: index 0 and 1 are equal on all 10500 rails
frames and all 8000 all-range frames, in the total and all 12 regions. The cameras drawn differ on 74 of 74 and 49 of 49
sampled frames, so the equal hashes are not two runs of one camera: `views` fails if they are, and it did when given one
log twice. Co-op off, one replay of 5800 frames into all-range, 1 player, base build against this branch: no difference
on the 5817 common frames, state, level, total and 12 regions. The two logs end 11 frames apart, both after the last
frame of the replay.

Observed (measured), draw writes on Corneria: 116 symbols changed, most of them plainly render state. In objects:
`obj.status` of actors, effects and shots (kills), `obj.rot` of effects and shots, `sfxSource` of all, `Actor.iwork[11]`,
`iwork[24]`, `fwork[29]`. In players: `camYaw` and `camPitch` of the player drawn only, `shadowing`, `sfxSource`,
`sfxVel`, `sfx`, `arwing.drawFace`, `state` once. Outside: `sCoGrangaWork`, `D_edisplay_801615D0`, the radio state, HUD statics.
After the fix, with index 1: no `obj.status` or `obj.rot` is written any more.

Observed (read), the mechanisms:

- **Draw frees what it does not see.** Shots (`src/engine/fox_beam.c:1067-1068`, `:1255-1256`), all-range effects
  (`src/engine/fox_edisplay.c:1487`), all-range items (`:1529-1530`), debris (`:1286`), bonus text
  (`src/engine/fox_effect.c:136`). Upstream already skips the shot and effect kills in versus, the one mode with several
  cameras (`!gVersusMode`), and skips the item kill unless `gCamCount == 1`.
- **Six places skip an object's draw function by camera.** `Actor_DrawAllRange`, `Boss_Draw` and `Effect_DrawAllRange`
  skip functions with game logic in them. `Item_Draw`, `Scenery360_Draw` and the sprite loop of `Object_DrawAll`
  (`src/engine/fox_edisplay.c:1919`, in all-range and during a level intro) skip functions that write nothing but the
  rotation in `ItemMeteoWarp_Draw`, so those three stay as they are. On rails `Actor_DrawOnRails` and `Scenery_Draw` have
  no such test.
- **Draw functions hold game logic, behind that test:** boss damage (`src/overlays/ovl_i3/fox_a6.c:1980`), boss state
  (`src/overlays/ovl_i1/fox_ve1.c:2546`, `src/overlays/ovl_i5/fox_ti.c:5452`), limb positions the boss update reads
  (`sCoGrangaWork`, `src/overlays/ovl_i1/fox_co.c:1187`), effects spawned from draw (`ActorEvent_Draw`,
  `src/engine/fox_enmy2.c:3894`), random numbers (`:4061`).
- **Boss AI reads a position in camera space.** `D_edisplay_801615D0` is the boss seen from the camera, with `.y` replaced
  by "in view" (`src/engine/fox_edisplay.c:1399`). Read at `src/overlays/ovl_i1/fox_co.c:356`, `:488`, `:514`, `:675-676`,
  `src/overlays/ovl_i4/fox_ka.c:888`, `src/overlays/ovl_i4/fox_sz.c:194`, `src/overlays/ovl_i6/fox_sy.c:990`, `:1234`,
  `:1711-1728`.
- **Limb callbacks are not in camera space.** `Animation_DrawSkeleton` starts `gCalcMatrix` from the `transform` it is
  given, apart from `gGfxMatrix` (`src/engine/fox_std_lib.c`), so a limb position depends on whether the draw ran, not on
  which camera ran it.
- **`gPlayerNum` was the local player during `Object_Update`,** and simulation reads it: `MeHopBot_Update`
  (`src/engine/fox_enmy2.c:156`), `func_enmy_80060FE4` (`src/engine/fox_enmy.c:106`), the laser at `:3092`.

Verdict of each write. **Moved out of the draw path in co-op** (`src/port/coop/game/view.c`):

| Write | Read by | In co-op |
|---|---|---|
| Kills of shots, effects, items, debris, bonus text, above | Every later frame: slots, collisions | Not freed for being out of view, as versus does for shots and effects. Their own timers end them |
| `Actor.iwork[24]` (`src/engine/fox_edisplay.c:1304`) | `src/engine/fox_360.c:1233`, a wingman near player 0 for 200 frames speaks | Always 1: every actor's draw runs, so all count as seen |
| `lockOnTimers`, not drawn (`:1285`) | `src/engine/fox_play.c:3313`, `:3378`, `:3426` | Does not run: all count as seen |
| `lockOnTimers`, too close (`:1166`, `:1297`, `src/engine/fox_enmy2.c:321`) | The same | Judged from the view of player 0 (`CoopView_LockOnZ`) |
| `camYaw`, `camPitch` (`src/engine/fox_display.c:1928-1929`) | 27 sites, for example `ItemSupplyRing_Update` | Worked out for every player before the draw, put back after it |
| `Player.xPath` (`src/engine/fox_bg.c:1344`, `:1352`) | Draw, the path code | Cleared for every player before the draw, so the draw's write changes nothing |
| `D_edisplay_801615D0`, `Boss.vwork[30]` (`src/engine/fox_edisplay.c:1391`, `:1402`) | Boss AI, above | The view of player 0 on every client, from a matrix every client builds |
| `obj.rot` facing the camera, effects and shots (`:2031`, `src/engine/fox_beam.c:1263`) | Draw | Put back after the draw |
| Object draw functions, boss damage and the rest above | Boss update, AI, slots, RNG | Run on every client, in view or not; what is out of view has its output dropped (`CoopView_DrawAnyway`) |
| `gPlayerNum` (`src/engine/fox_play.c:7052`, `:7069`, `src/engine/fox_game.c`) | Above | Local only inside `Game_Draw`; 0 for the simulation, and for the HUD |

**Left out of the hash in co-op** (`ClearViewState`, `ClearPlayerViewState`, `src/port/coop/sync/state_hash.c`), each read
only by the sound engine or draw code, checked at every read site:

| Value | Written | Why it may differ |
|---|---|---|
| `sfxSource` of actors, bosses, effects, items, scenery, shots | `Object_SetSfxSourceToView`, `Object_UpdateSfxSource` | Sound position from the camera. Read by sound and `src/engine/fox_display.c:96` |
| `Player.sfxSource`, `sfxVel`, `sfx` | `src/engine/fox_display.c:1748-1765` | The same; `sfx` is read in `src/audio/audio_general.c` |
| `Player.shadowing` | `:861-863` | Written for a player the camera sees. Read at `:852` |
| `arwing` flap angles, `unk_28` | `:782-783` | Cleared for a player the camera sees in space levels. Feed only themselves and draw |
| `obj.rot` of `OBJ_ACTOR_SX_SLIPPY`, `obj.rot.y` of `OBJ_ITEM_METEO_WARP` | `src/overlays/ovl_i2/fox_sx.c:181-182`, `src/engine/fox_edisplay.c:930` | Faces the camera, one frame late, so it cannot be put back like an effect's. Read by draw only |

**Shown harmless:**

| Write | Why |
|---|---|
| HUD, pause and level clear logic: `gBombCount` (`src/engine/fox_hud.c:3457`), `gLifeCount` (`:1089`, `:1539`, `:1552`), `gPlayer[0].state` (`:1485`), `gPlayer[0].heal` (`:2440`) | Same on every client: runs every frame by shared state, and `gPlayerNum` is 0 there now. Only slot 0 is clamped and credited |
| `gRadioState` and radio state (`Radio_Draw`, `src/engine/fox_radio.c:534`) | Runs every frame by shared state, no camera in it. Audio timing is another question, under Unknown |
| `gHitCount` clamp (`src/engine/fox_display.c:1845`), `arwing.drawFace` (`:1839`), `gTeamHelpTimer` (`:58`), `gGroundSurface` (`src/engine/fox_bg.c:1505`) | No camera in them: the same on every client |
| `Actor.iwork[11]`, `fwork[29]` | Engine glow, set by draw functions that now always run. Read only by draw (`src/engine/fox_edisplay.c:479-485`) |
| `gRadarMarks` | Hashed in `misc`, read only by the HUD, and written from shared state |
| `gTeamArrowsViewPos`, `gLockOnTargetViewPos`, `sPlayersVisible`, `sViewPos`, `sShotViewPos`, `gPlayCamEye`, `gPlayCamAt`, `gSunViewX`, `gLight1x`, `y`, `z`, `gLight2x`, `y`, `z`, `gStarfield*`, `D_display_*` | Camera results that only draw reads: outside draw nothing reads them, apart from clearing at level start |
| `gPlayerGlare*` (`src/engine/fox_bg.c:1083`) | The same, and Andross fades its own glare from its own value (`src/overlays/ovl_i6/fox_andross.c:1047`, `:4370`). Nothing else follows from it |
| 124 of the 307 globals written in draw | Nothing outside draw mentions them |
| The other globals draw writes and simulation code mentions: pause and continue state (`HUD_PauseScreen_Update`), level start clears (`Play_ClearObjectData`), light colours and targets (`Camera_SetupLights`), limb tables in the overlays, radio and HUD timers | Grouped here, not listed one by one. No camera value feeds them: 64 draw functions read a camera value or `gPlayerNum`, and what each writes is in these tables, or is a local, draw-only or versus-only. The code runs on every client, so the values match |
| `Object_Kill` in `Macbeth_TrainTrack_Draw` (`src/overlays/ovl_i5/fox_ma.c:2624`, `:2633`) | Reached through `Scenery_Draw`, which has no view test, and judged by `gPlayer[0]` |
| `Matrix_GetYRPAngles(gGfxMatrix)` (`src/overlays/ovl_i5/fox_ti.c:2558`, `:3121`) | Only undoes a rotation for a billboard, then draws |
| `Rand_ZeroOneSeeded` in `Bolse_DrawDynamicGround` (`src/overlays/ovl_i4/fox_bo.c:2238`) | Drawn before its culling test, from a seed set each frame, so the sequence does not depend on the view |

Choices this makes, all reversible, none of them a game rule with a record (`DESIGN.md` section 4):

- The camera the simulation uses is player 0's. For player 0 that is single-player behaviour.
- A wingman's "near player 0" line no longer needs the wingman on screen.
- What leaves the view lives on until its own timer.
- `gBombCount` is clamped and level clear lives credited for slot 0 only, as any client's HUD draws through `gPlayerNum`
  0. WP-28 moves the HUD to the local player: these writes must stay on slot 0 or go through every player.

Derived:

- Rendering is not on top of game logic: `Game_Draw` runs once per tick and changes state. The Derived point in
  `DESIGN.md` section 2 was wrong as written, and no earlier determinism run could show it, as all drew player 0.
- Lockstep with one camera per client does not need a design change on this evidence: every client has every player's
  camera, so all can agree on one camera for the simulation.
- WP-22 can rely on the hash with different local indexes, on Corneria.
- This replaces older lines. `gPlayerNum` is no longer the local player between the player loops, in the #8 entry
  and the #67 entry: it is 0 there, and local only inside `Game_Draw`. The Derived point of #67 that WP-22 can rely on the
  hash only while `CoopPlayers_LocalIndex()` is 0 no longer holds on Corneria.

Unknown:

- Every level but Corneria: the verdicts there come from reading, not from a run.
- Whether effects with no timer of their own pile up now that nothing frees them for leaving the view. Versus runs that
  path upstream, but only on versus stages. The runs here did not count slots.
- Whether the sound and picture look right for a client through player 1: these runs check state, not what is seen.
  Before player 1 joins it looks through a parked player's empty camera, which WP-28 has to settle.
- Local indexes 2 and 3: only 0 and 1 ran.
- A camera value that reaches a write through a local or a parameter: the read searched for the names of camera values,
  so it sees such a path only where a function was read by hand.
- `Katina_EnemyDraw` picks low poly by `gSysFrameCount` into `iwork` (`src/overlays/ovl_i4/fox_ka.c:2633`): not a camera
  matter, but two clients need not share that count, and `iwork` is hashed.
- `Radio_Draw` follows the voice status (`src/engine/fox_radio.c:645`), which comes from the sound thread. WP-02's audio
  variants matched on one machine; two machines did not run.

### 2026-11-06, #67, state hash: per-player arrays, team arrays, co-op slots

Identity: ROM US 1.0, branch `issue/67-hash-player-arrays` on top of `95962d04`, Linux, GCC, Debug, OpenGL on a software
renderer, no window, one game per run, no transport. Input by rule (`tools/coop/hash_check.py`,
`write_generated_replay`): 5800 frames, Corneria, into all-range. These runs are Corneria only, on one machine.

Method: every array in `include/sf64context.h` with four slots, then every array the code indexes by player anywhere
in `src/` and `include/` (`git grep` for `[gPlayerNum]`, `[player->num]`, `[this->num]`, `[this->playerNum]`,
`[shot->sourceId]`, `[gMainController]`), then each read site of what that found. All of it read in this checkout.

Hashed, in the `players` region, in co-op only (`sPlayerArrays`, `src/port/coop/sync/state_hash.c`):

| Value | The simulation reads it at |
|---|---|
| `gLeftWingHealth`, `gRightWingHealth` | `src/engine/fox_play.c:940`, `:957`: the wing breaks at 0 |
| `gLaserStrength` | `src/engine/fox_play.c:3173` which shot, `src/engine/fox_beam.c:671`, `:1023` what it does |
| `gChargeTimers` | `src/engine/fox_play.c:3350`, `:3356`, `:3400`: charge and lock-on shot |
| `gUturnDownTimers`, `gUturnBrakeTimers` | `src/engine/fox_play.c:5275-5288`: the U-turn input |
| `gLoopDownTimers`, `gLoopBoostTimers` | `src/engine/fox_play.c:5143-5155`: the loop input |
| `gLeftWingDebrisTimer`, `gRightWingDebrisTimer` | `src/engine/fox_play.c:240`, `:251`: spawn effects and draw random numbers |
| `gHasShield`, `gShieldTimer`, `gShieldAlpha` | `src/engine/fox_play.c:1012`, `:5543`, `:934`: `gShieldAlpha` decides whether wings take damage |
| the slot of each player | `src/port/coop/game/players.c`: parked or not decides what `Player_Update` and the camera run |

Hashed, in the `globals` region, in co-op only (`sTeamArrays`). These hold one value per team member, and WP-17 puts
human players in those slots. Only the slots listed count, and a probe checks that the others do not:

| Value | Slots | The simulation reads it at |
|---|---|---|
| `gTeamDamage` | Falco to Peppy | `src/engine/fox_play.c:7029-7036`: pending damage, drained into `gTeamShields` by 2 a frame. No level start clears it. Two different amounts drain alike until the smaller one ends, and writing 0 can remove the difference for good (`src/engine/fox_360.c:1467`, `:1479`, `src/engine/fox_enmy2.c:2091`, `:3338`), so `gTeamShields` does not stand in for it |
| `gSavedTeamShields` | Falco to Bill | `src/engine/fox_play.c:2935-2936` restored at level start, `:545-551` converted first |
| `gPrevPlanetTeamShields` | Falco to Bill | `src/engine/fox_hud.c:1587-1591` restored on a retry. `src/engine/fox_play.c:4923` only writes it |
| `D_ctx_80177C58` | Falco to Bill | `src/overlays/ovl_menu/fox_map.c:3626-3629`: `Map_RetryCourse` restores three arrays from it |
| `gTeamEventActorIndex` | all 4 | `src/engine/fox_enmy2.c:1453` script target, `src/engine/fox_effect.c:2885-2886` shots against team actors. No level start clears it |
| `D_enmy_Timer_80161670` | all 4 | `src/engine/fox_enmy2.c:2789`, `:2795` event conditions: is this team member being chased (`:3238-3239`) |

Hashed before this entry, in the `globals` region, with co-op off too: `gLifeCount`, `gBombCount`, `gPlayerScores`.
`gGoldRingCount` is hashed but is not per player: `[0]` is the count and `[1]` what the HUD has shown of it
(`src/engine/fox_hud.c:364-365`).

Not hashed, and why:

| Value | Reason |
|---|---|
| `gRightWingFlashTimer`, `gLeftWingFlashTimer` | Only drawn (`src/engine/fox_display.c:529-582`); the simulation only counts them down (`src/engine/fox_play.c:5525-5534`) |
| `gMuzzleFlashScale` | Only drawn (`src/engine/fox_display.c:1133-1203`), and decays (`src/engine/fox_play.c:5550`) |
| `D_ctx_80177990`, `D_ctx_801779A8`, `gControllerRumbleTimers` | Rumble only (`src/engine/fox_play.c:5897-5907`); many writes use `gMainController`, which is local to a client |
| `gControllerRumbleFlags`, `gControllerRumbleEnabled`, `gControllerRumble` | Rumble hardware (`src/sys/sys_joybus.c:111-132`) |
| `gBoostButton`, `gBrakeButton`, `gShootButton`, `gBombButton` | Written once, the same constant on every client (`src/engine/fox_game.c:438-441`) |
| `gVsLockOnTimers` | Stays 0 outside versus: its only non-zero write is under `gVersusMode` (`src/engine/fox_beam.c:1757-1766`) |
| `gVsPoints`, `gVsKills`, `gHandicap`, `gPlayerForms`, `gPlayerInactive` | Read only in versus (`src/engine/fox_play.c:4580`, `:4745-4751`, `:5985-5986`, `src/engine/fox_versus.c`). `gPlayerInactive` also at `:6087`, `:6346`, reached only from `PLAYERSTATE_VS_STANDBY` |
| `D_ctx_80177858`, `D_ctx_80177958`, `D_ctx_801783C0` | Written, never read (`src/engine/fox_context.c:144`, `:304`, `:316`) |
| `gPlayerGlareAlphas`, `gPlayerGlareReds`, `gPlayerGlareGreens`, `gPlayerGlareBlues` | Written by draw code for the camera being drawn (`src/engine/fox_bg.c:1083-1115`), read by draw code (`src/engine/fox_game.c:566-591`) |
| `gShowReticles`, `sReticleScales`, `sPlayersVisible`, `gLockOnTargetViewPos`, `D_display_801613B0`, `D_display_801613E0`, `D_display_80161418`, `D_display_80161518`, `D_display_80161548`, `D_display_801615A8`, `D_display_801615B8` | Draw state (`src/engine/fox_display.c`); what draw code does with them to hashed objects is under "Derived" |
| `D_80161738`, `D_80161748`, `D_80161760`, `D_800D19AC`, `D_800D19E0`, `sVsSavedBombCount`, `sVsBombCountFlashTimer` | HUD state (`src/engine/fox_hud.c`) |
| `sPlayerRespawnTimer`, `sPlayerDownFrames`, `sVsDisplayedScores`, `D_801787D8`, `D_801787E8`, `D_80178810`, `D_80178850`, `sPlayerFormOption` | Versus only (`src/engine/fox_versus.c:18-44`) |
| `gSavedGoldRingCount` | One value, `[0]` only (`src/engine/fox_play.c:4677`): a checkpoint, not per player |
| `gPlayerNum`, `gMainController` | Local to a client: `gPlayerNum` is `CoopPlayers_LocalIndex()` between player loops (`src/engine/fox_play.c:7046-7048`), and they would differ between clients with nothing wrong |
| `gInputHold`, `gInputPress` | Pointers, and input is not state |
| `gControllerHold`, `gControllerPress`, `gControllerPlugged` | Input, which is what lockstep exchanges; a wrong pad shows in `Player` on the next frame |
| `gPlayerTurnRate`, `gPlayerTurnStickMod` | Scratch: set before each use in the same function (`src/engine/fox_play.c:3758-3759`, `:4001-4002`, `:4210-4211`, `:4377-4378`, `src/engine/fox_tank.c:404-405`, `src/overlays/ovl_i3/fox_aq.c:886`, `:908`) |
| `gPathVelX`, `gPathVelY` | One value all players write, the step `xPath` and `yPath` took, which `Player` holds (`src/engine/fox_play.c:3676-3677`); read only for effect velocities, which are hashed (`src/engine/fox_effect.c:2459-2460`) |
| `gCsCamEyeX` and friends | Scratch for the camera update, which ends in `Player.cam` (`src/engine/fox_play.c:6325-6328`) |
| `gPrevPlanetSavedTeamShields` | Written, never read: every site is a store (`src/engine/fox_play.c:4923`, `src/overlays/ovl_menu/fox_map.c:1453`, `src/overlays/ovl_i1/fox_ve1.c:2517`, `src/overlays/ovl_i3/fox_a6.c:2912`, `src/overlays/ovl_i4/fox_bo.c:1663`, `src/overlays/ovl_i6/fox_ve2.c:117`, `:438`) |
| Slot of Fox in the team shield arrays, slots of Fox, Katt and Bill in `gTeamDamage` | Not read as a team member's: the loops start at Falco (`src/engine/fox_play.c:2935`), `gTeamDamage` drains Falco to Peppy only (`:7029`), so a value there can stay from an old level for good |
| `gMissionTeamStatus` | One value per mission, a bit per team member. Read for the ending, the ranking and the map (`src/overlays/ovl_ending/fox_end2.c:239-241`, `src/overlays/ovl_menu/fox_option.c:3999`, `src/overlays/ovl_menu/fox_map.c:5704`), not by play |
| `gCsTeamTargetsX`, `gCsTeamTargetsY`, `gCsTeamTargetsZ` | Cutscene scratch worked out from `player->pos` and `gActors[0]` (`src/engine/fox_demo.c:1160-1186`); the actors that follow it are hashed |
| `gTeamArrowsViewPos` | Draw state, by team member (`src/engine/fox_edisplay.c:1214`, `:1257`) |
| `gStarWolfTeamAlive`, `gSavedStarWolfTeamAlive` | Enemies, not team members: no player goes there. Level state, like most globals the hash leaves out |
| `D_ctx_80177C94` to `D_ctx_80177CC4` | Player 0 only: carried from Venom into Andross, and hashed once restored (`src/engine/fox_play.c:4690-4698`, `src/overlays/ovl_i1/fox_ve1.c:2524-2530`) |

Inside hashed objects: `Actor.lockOnTimers[4]` and the same field of other objects are per player and covered with
the object. `Player` is hashed whole.

Observed (runs):

- **The hash missed all of it.** On the test commit `edca28d4` (hash logic as on `95962d04`),
  `hash_check.py probe --players 2` failed 64 checks. No per-player array showed for player 0 (0 of 14 probes) or for
  player 2 in the level (0 of 4), no team array showed (0 of 14), and no slot showed (0 of 14, 0 of 10, 0 of 4).
- **`gLifeCount`, `gBombCount` and `gPlayerScores` are hashed, as the issue said.** On the same commit each showed in
  `globals` only, for player 0 (14 of 14) and for player 2 in the level (4 of 4).
- **They also showed too much:** a parked player's slot and the slots of players who do not exist changed the hash
  (0 of 10 and 0 of 14 as expected). No level start clears those slots (`gBombCount` only in `Player_ResetVsData`,
  `gLifeCount` not at all; `gPlayerScores` only below `CoopPlayers_Count()`, `src/engine/fox_play.c:2953-2957`), so
  a client that played versus or another co-op level first would differ from a fresh one.
- **After the fix** (`14a353d2`): `probe --players 2` and `probe --players 4` pass. Each hashed value shows in its one
  region for every player in the level (14 of 14 for player 0, 4 of 4 for the others), every team slot that counts
  shows (14 of 14), and a parked player, a player who does not exist and a slot nobody reads leave the hash alone.
- **Hashing them keeps two runs equal.** `run --players 2 --poke-player 1`: A and B equal on all 5800 recorded frames,
  a poke in player 2 at 5400 first seen at 5400, in `players`. `run` with 1 player: the same, the poke seen in `actors`.
- **Co-op off is unchanged.** `hash_check.py diff` of a plain 1-player run of `95962d04` and of this branch: no
  difference on 5821 common rows in game state, level, total and all 12 regions, and the same for a second pair on
  5809 rows. The logs end 8 and 13 rows apart, past the 5800 recorded frames, because the tool stops the game once the
  report exists. `probe --players 1` passes before and after the fix: the arrays new in co-op do not show, and the
  three older ones do, in all four slots.

What changed (`src/port/coop/sync/state_hash.c`, accessor at the end of `src/port/coop/game/players.c`):

- In co-op only: `sPlayerArrays` and the slots go into `players`, `sTeamArrays` into `globals`. No new region, so a log
  keeps its columns.
- In co-op a per-player slot counts only while its player is in the level, for the older three values as well. With
  co-op off the bytes hashed are the same as before.
- `hash_check.py diff` compares every column. It used to compare the frame and the total.

What the hash cannot cover from `state_hash.c`, or cannot tell apart per player:

- **Landmaster:** its state is one value for all players, not a slot each: `D_800C9F00`, `D_800C9F04`, `D_800C9F08`,
  `D_800C9F10`, `D_800C9F14`, `D_800C9F20`, `D_800C9F24`, `D_800C9F28` (`src/engine/fox_tank.c:14-23`), read by the
  simulation (`:449-450`, `:481`, `:878-881`, `:1129`). Not hashed. `extern` would reach them, but a value shared by
  two tanks says nothing per player.
- **Blue Marine:** the same in `D_i3_801C41B8` (`src/overlays/ovl_i3/fox_aq.c:202`, read in `Aquas_BlueMarineMove`,
  `:896-942`). Not hashed.
- Neither matters today: a co-op player joins only in all-range (`CoopPlayers_SkipUpdate`), so only player 0 drives
  one. WP-18 has to give them a slot per player, and then they belong in `sPlayerArrays`.
- `D_800C9F3C` is a function-local static (`src/engine/fox_tank.c:599`), which no other file can reach. It only keeps a
  sound from restarting (`:666-671`, `:690`).
- **Draw code writes into hashed objects, for the camera being drawn:** `sfxSource` of actors, bosses, items, scenery
  and shots (`Object_SetSfxSourceToView`, `src/engine/fox_edisplay.c:1282`, `:1305`, `:1431`, `:1472`,
  `src/engine/fox_beam.c:2376`), `Actor.iwork[24]` (`src/engine/fox_edisplay.c:1283`, read by the all-range AI at
  `src/engine/fox_360.c:1233`), `Actor.lockOnTimers[gPlayerNum]` (`src/engine/fox_edisplay.c:1266`, `:1276-1277`) and
  a kill of debris that was not drawn (`:1267-1269`). Every client draws player 0 today.
- **`gPlayerNum` outside the player loops is the local player:** `HUD_BombCounter_Draw` clamps
  `gBombCount[gPlayerNum]` (`src/engine/fox_hud.c:3456-3457`), the level clear screen adds to `gLifeCount[gPlayerNum]`
  (`:1088-1092`), `Player_Setup` resets `gLaserStrength`, `gLifeCount` and `gBombCount` by it
  (`src/engine/fox_play.c:4928-4931`).

Derived:

- Once WP-28 gives each client its own view, the draw writes above differ between two clients with nothing wrong, and
  `iwork[24]` and `lockOnTimers` then change the simulation. WP-22 can rely on the hash as long as
  `CoopPlayers_LocalIndex()` is 0 on every client, not after that, until these move out of draw code or out of the
  hash.
- The same goes for the `gPlayerNum` writes: the hash would show them, correctly, as a real difference.
- The hash in co-op now depends on values no level start clears (`gTeamDamage`, `gTeamEventActorIndex`), and on the
  saved team arrays. The simulation reads them, so that is right, but two clients with a different past differ there
  from their first co-op frame. WP-24 has to start clients from the same values.
- `sCoopPlayerCount` is not hashed as a value: it shows as the length of the `players` region.

Unknown:

- Levels other than Corneria: none was run, so whether any of these values ever differ between two clients there.
- Whether the values actually moved in these runs: a probe flips a value, it does not say what the value was.
- Whether a co-op player's values stay equal between two runs through a boss, a retry or a level change: not run.
- Two real clients: everything here is one process.

### 2026-11-06, #53, state hash: every co-op player, negative numbers, free slots

Identity: ROM US 1.0, branch `issue/53-hash-players` on top of `a69c811a`, Linux, GCC, Debug, OpenGL on a software
renderer, no window, one game per run, no transport. Input by rule (`tools/coop/hash_check.py`,
`write_generated_replay`): 5800 frames, Corneria from frame 3096, all-range from frame 4880, player 2 flying from
between frames 5000 and 5200. These runs are Corneria only, on one machine.

Observed (code):

- `CoopPlayers_Count()` returns `gCamCount` unless that is 1, and then the count fixed in `Play_Init`, which stays 1
  while `gCoopDebugPlayerCount` is unset (`src/port/coop/game/players.c:26-50`). `CoopPlayers_IsCoop()` is 0 before the
  first level, even with the CVar set.
- `Memory_Allocate` does not clear (`src/sys/sys_memory.c:12-17`). `gScenery360` comes from it, and the game then sets
  only `obj.status` of each slot (`src/engine/fox_play.c`, `if (gChangeTo360)` and `Play_Init`).
- The menus put the star field at the start of the same buffer, 36000 bytes (`src/engine/fox_play.c:642-644`,
  `src/overlays/ovl_menu/fox_title.c:196-198`). Space levels and Training set up a star field before `gPlayer`
  (`src/engine/fox_play.c:2906-2907`), so there `gScenery360` lies behind it.
- The game writes negative numbers to `iwork`: `-1` (`src/engine/fox_360.c:1367`, `src/overlays/ovl_i6/fox_sy.c:3605`,
  `src/overlays/ovl_i5/fox_ma.c:1168`) and unclamped steps (`this->iwork[0] -= 7` while it is not 0,
  `src/engine/fox_demo.c:2897-2899`). No store truncates a pointer to 32 bits: every pointer store into `iwork` uses
  `(uintptr_t)` (grep, `src/overlays/ovl_i5/fox_ti.c` only).
- `sRandSeed1..3` are multiplied modulo the primes 30269, 30307 and 30323 (`src/sys/sys_math.c:29-31`).

Observed (runs):

- **Two Linux runs of the same input matched** on every frame both logs hold, including `rng`: 1 player (5801 frames
  before the fix, 5800 after), 2 players (5803 before, 5800 after), and 2 players with L to level complete (12007,
  after). Other games and builds ran on the machine at the time.
- **Player 2 was not hashed.** `hash_check.py probe --players 2` on the test commit: flipping a bit of player 2 changed
  nothing, parked (10 of 10 probes) or flying (4 of 4), while player 0 showed on 14 of 14.
  `hash_check.py run --players 2 --poke-player 1`: with the bit flipped at frame 5400, the first hash difference was
  at frame 5401, in `effects` and `shots`. So the hash saw only what player 2's difference did to other objects, a
  frame late. After the fix: 14 of 14 probes, and the first difference is at frame 5400, in `players` only.
- **Co-op off is unchanged.** The 1-player log of `a69c811a` and of this branch are equal in frame, game state, level,
  total and all 12 regions on all 5801 frames.
- **Negative numbers collapse:** with `iwork` at -1 and at -2, the hash stayed the same (14 of 14 probes). **No natural
  case was seen:** no actor held an `iwork` value above 32 bits on any frame of the three measured runs (5800 frames
  with 1 player, 5800 and 12000 with 2), neither negative numbers nor foreign pointers.
- **Free slots hold old bytes:** in Corneria's all-range section 67 slots are active and 133 free, and all 133 are not
  zero on every measured frame. Flipping a bit in one changed the hash (5 of 5 probes). One-player and 2-player runs
  first differ at frame 4880, in `scenery360` only, before player 2 has joined: the active slots hash the same in both
  (`3575d50e9bbd95cf`), the free ones do not (`f8a1b945780158fb`, `f21f981611287fec`). `gScenery360` starts 1264 bytes
  behind `gPlayer` with 1 player and 2528 with 2.

What changed (`src/port/coop/sync/state_hash.c`):

- `CoopPlayers_Count()` at the three player sites. `gCamCount` stays in the globals, as a value.
- In co-op only, a negative number stays what it is, and a free `gScenery360` slot counts as empty. With co-op off both
  stay as they were, so single-player logs still line up with older ones (`docs/coop/QA.md`, test plan item 1).

Derived:

- A non-zero seed below its prime modulus never becomes 0, so the re-seed in `Rand_ZeroOne` (`src/sys/sys_math.c:24`)
  fires only if a seed starts at 0 or something else writes one. A replay's seeds are not 0, which fits the equal `rng`
  regions above. This narrows the WP-02 note, it does not close it: the upstream comment says something corrupts the
  seeds on Linux, and the check still reads `sRandSeed2` twice and `sRandSeed3` never.
- Two-run comparisons are not blocked on Linux for this input.
- In Corneria the free slots lie inside the star field block, so with one player count their bytes should be the same
  for every client.

Guess:

- The bytes in Corneria's free slots are the star field. Follows from the code above; the bytes were not compared.
- In space levels the bytes behind the star field survive the menus, so a client that played another all-range level
  before the session would hash differently from one that did not. Not run. It is the case the free slot fix is for.

Unknown:

- Other levels: none was run, so whether the game reaches a negative `iwork` value, and whether free slots ever differ
  between clients with the same player count.
- The frames between two levels with 2 players. In this input the level-complete state lasts past frame 12000, so no
  second level was reached, and frames where `gPlayer` still points at the last level's memory did not run.
- Other platforms and compilers: the Linux and Windows runs used different input.
- State that is still not hashed for any player: the per-player arrays such as `gLeftWingHealth`, `gLaserStrength` and
  `gChargeTimers`, and the co-op slot state in `src/port/coop/game/players.c`.

### 2026-11-04, #3 (WP-03), input replay

Identity: ROM US 1.0, branch `issue/3-input-replay` on top of `ba0781d7`, Windows 11, MSVC 19.44 (VS 2022 Build
Tools), Debug, DirectX 11.

Observed:

- Controller input enters the game once per game frame: `push_frame` (`src/port/Game.cpp:23`) calls
  `Graphics_ThreadUpdate` (`src/sys/sys_main.c:232`), which runs `Controller_UpdateInput` then `Controller_ReadData`
  (`src/sys/sys_main.c:241-242`) and then `Game_Update`. `Controller_UpdateInput` copies the pads read in the previous
  frame into `gControllerHold[4]` and computes `gControllerPress[4]` (edges and dead zone) (`src/sys/sys_joybus.c:65-82`).
  `Game_Update` fires `GamePreUpdateEvent` before any game logic (`src/engine/fox_game.c:352`). Between the two only
  `Main_SetVIMode` reads input (gamma toggle, `src/sys/sys_main.c:160`).
- The port's `OSContPad` is 0x24 bytes and carries gyro and right stick fields next to the N64 ones
  (`libultraship/include/libultraship/libultra/controller.h:98-107`).
- RNG seeds are plain globals: `sRandSeed1-3` and `sSeededRandSeed1-3` (`src/sys/sys_math.c:4-9`). `Rand_Init` fills
  them from `osGetTime()` (`src/sys/sys_math.c:15-18`), called from `Game_Initialize` (`src/engine/fox_game.c:58`).
  The seeds observed at runtime are often negative (for example `-28555`), because the 64-bit time is cast to `s32`
  before `% 30000`.
- The game runs about 30 logic frames per second: 1251 to 1277 frames in a 45 s idle run.
- **Wall-clock timer:** the port's `Timer_CreateTask` / `Timer_Update` fire on `SDL_GetTicks()`
  (`src/port/Engine.cpp:687-734`). The only game caller is the Nintendo logo at boot, which advances `gGameState` after
  1000 ms (`src/engine/fox_game.c:402`). A first replay that started at the first game frame diverged at frame 28
  (`GSTATE_LOGO_WAIT` against `GSTATE_START`), because the logo lasted a different number of frames.
- With recording and playback starting at the first frame of `GSTATE_START` (after the logo) and the six RNG seeds
  restored there, a 1251-frame idle run (title, intro, attract demo) replays with every per-frame checkpoint equal.
- **Corneria:** a human run recorded from `GSTATE_START` through the menus, Corneria and on to the map (13545 frames,
  465 s, level complete at frame 11747 on level 0) played back unattended in a fresh folder with every per-frame
  checkpoint equal and level complete at the same frame 11747: `replay: PASS (play, 467s)`.

Derived:

- Inputs plus the six seeds at `GSTATE_START` are enough to reproduce the menus and a full Corneria run frame by frame
  on one machine and one build, as far as the checkpoint sees (game state, level, player 0 state and position, RNG
  seeds). This is a first positive sign for WP-02, not its result: WP-04 hashes far more state.
- For lockstep, a session has to start after boot (at a level start or a menu), not at process start, or the logo
  timer has to become frame-based.

Unknown:

- Whether state outside the checkpoint (enemies, bosses, wingmen, items) also stays identical. WP-04 will tell.
- Whether a replay recorded on one PC plays back on another with the same build (WP-02).
- Whether audio or other wall-clock sources affect other levels.

### 2026-11-04, #4 (WP-04), state hash

Identity: ROM US 1.0, branch `issue/4-state-hash` on top of `070256bd` (#37), Windows 11, MSVC 19.44 (VS 2022 Build
Tools), Debug, DirectX 11.

Observed:

- The simulation state is: `gPlayer` (`gCamCount` entries, allocated per level, `src/engine/fox_play.c:2948`), the
  fixed arrays `gScenery[50]`, `gSprites[40]`, `gActors[60]`, `gBosses[4]`, `gEffects[100]`, `gItems[20]`,
  `gPlayerShots[16]`, `gTexturedLines[100]`, `gRadarMarks[65]`, `gBonusText[10]` (`include/sf64context.h:232-241`),
  `gScenery360` (200 entries, all-range levels only, `src/engine/fox_play.c:2957-2958`), the six RNG seeds
  (`src/sys/sys_math.c:4-9`) and globals in `src/engine/fox_context.c`.
- Pointer fields: `ObjectInfo.draw/dList`, `.action`, `.hitbox` in every object (`include/sf64object.h:145-160`),
  `Effect.unk_74` (`include/sf64object.h:229`), `Player.sfx.srcPos/srcVel` (`include/sf64player.h:159-160`).
  `Actor.iwork[25]` is `uintptr_t` and some actors keep pointers to other actors in it
  (`src/overlays/ovl_i5/fox_ti.c:694-697`, `:1733-1734`).
- Every object and the players are cleared byte by byte before use (`src/engine/fox_enmy.c:156-210`,
  `src/engine/fox_play.c:2605-2612`), so struct padding holds zeros unless the game writes it.
- `GamePostUpdateEvent` does not fire every frame: `Game_Update` returns early on standby (`src/engine/fox_game.c:355-358`)
  and the event sits inside a branch (`:614`). `GamePreUpdateEvent` fires every frame (`:352`).
- **Listener order:** listeners of one event are sorted by ascending priority (`src/port/hooks/impl/EventSystem.cpp:27-29`),
  so `EVENT_PRIORITY_LOW` runs first and `HIGH` last; equal priorities have no defined order (`std::sort` is not
  stable). The replay listener is now `LOW` so the hash (`NORMAL`) sees the restored seeds on frame 0.
- `python tools/coop/hash_check.py run local/replays/corneria.sfcr` (the 13545-frame Corneria run from WP-03), three
  games at once: runs A and B gave equal hashes on all 13545 frames; run C, with one bit of
  `gActors[59].fwork[29]` flipped at frame 6772, first differed at frame 6772 (region `actors`), then on 2847 frames up
  to 9618, and only in region `actors`. All three replays: `PASS`. 467 s.
- The total hash took 13536 distinct values over 13545 frames.

Derived:

- On one machine and one build, the whole hashed state (not just the WP-03 checkpoint) of a full Corneria run is
  reproduced exactly by inputs plus seeds. This is the "one machine" half of WP-02.
- The poke stopped showing at frame 9619, most likely because the game reused and cleared actor slot 59 (guess, not
  checked). The simulation itself did not change, since only `actors` differed.
- The pointer handling is enough to hide address randomisation between two processes of the same build.

Guess:

- Values in `Actor.iwork` below 2^32 are numbers, values above are pointers. True for 64-bit Windows and Linux with
  ASLR (images and heaps above 4 GiB), not checked on macOS or 32-bit builds.

Unknown:

- Whether the hash holds across two PCs, other compilers or Release builds (WP-02).
- State outside the hashed set that still affects the game (for example overlay statics, `gLevelObjects` progress
  beyond `gObjectLoadIndex`, audio). A desync that the hash misses would show up as equal hashes with different play.
- `gScenery360` is only hashed in `LEVELMODE_ALL_RANGE`; Andross allocates it mid-level
  (`src/overlays/ovl_i6/fox_andross.c:4055`), where it may not be hashed.

### 2026-11-04, #2 (WP-02), determinism spike on one PC

Identity: ROM US 1.0, branch `issue/2-determinism` on top of `e8c4e6a2` (#40), Windows 11, MSVC 19.44 (VS 2022 Build
Tools), Debug, DirectX 11 unless a run says otherwise. One PC: the comparison across two machines is not done.

Observed (code):

- **Order within one frame:** `push_frame` (`src/port/Game.cpp:23-30`) runs `Graphics_ThreadUpdate`, which does the
  whole game update and then `Audio_Update` (`src/sys/sys_main.c:232-281`). Only after that does it wake the audio
  thread (`StartAudioFrame`) and wait for it (`EndAudioFrame`, `src/port/Engine.cpp:420-435`). Game logic and audio
  never run at the same time.
- Per game frame the audio thread renders `gVIsPerFrame` audio updates (`src/port/Engine.cpp:383`, `:399-402`). Each
  update first advances the sequences by a fixed `ticksPerUpdate` (`src/audio/audio_synthesis.c:665-668`). Only then
  does it render a sample count picked from the output device's fill level: 560 or 528 at 32 kHz
  (`src/port/Engine.cpp:349-350`, `:387-388`). That fill level depends on wall-clock time.
- Game logic reads audio state: the radio waits for the voice sequence (`Audio_GetCurrentVoice`,
  `Audio_GetCurrentVoiceStatus` in `src/engine/fox_radio.c:615-645`, `src/overlays/ovl_menu/fox_map.c:6014`, `:6045`;
  defined in `src/audio/audio_general.c:1890-1925`). `Audio_GetCurrentVoiceStatus` checks whether the voice layer
  still holds a note.
- `gAudioRandom` mixes in `osGetCount()` (`src/audio/audio_thread.c:87`, `:165`). It is only read in `src/audio/`
  (sound pitch, `src/audio/audio_general.c:499`, `:2000-2001`).
- Clock reads that can reach the simulation: `Rand_Init` (`src/sys/sys_math.c:15-18`; the replay restores the seeds)
  and the boot logo timer (`src/engine/fox_game.c:402`, see #3). `osGetTime` in `src/engine/fox_versus.c:1657` is
  versus mode only. No other `osGetTime`, `osGetCount`, `SDL_GetTicks`, `rand()` or `<chrono>` in `src/engine`,
  `src/overlays` or `src/sys` outside `sys_fault.c` (searched with grep).
- The Linux and Switch re-seed in `Rand_ZeroOne` checks `sRandSeed2` twice and `sRandSeed3` never
  (`src/sys/sys_math.c:24`). Seeding from the clock in the middle of a level breaks lockstep on those platforms.
- `__sinf` and `__cosf` (and so `SIN_DEG`/`COS_DEG`) call the C runtime's `sinf` and `cosf`
  (`src/port/Engine.cpp:664-677`). On Windows x64 the MSVC runtime picks an FMA3 code path when the CPU has one;
  `_get_FMA3_enable()` returned 1 on this PC.
- Game code reads gameplay CVars such as `gRapidFire`, `gInvincible`, `gUnbreakableWings`, `gForceExpertMode`,
  `gSzActorFix`, `gMaCameraFix` and `gRestoreBetaBombExplosion`
  (`grep -rn 'CVarGetInteger("g' src/engine src/overlays src/sys`). Clients that differ in any of them simulate
  different games.
- The state hash log was flushed only every 60 frames (`src/port/coop/sync/state_hash.c`, WP-04). When a test tool
  stopped the game right after the replay report, the end of the log was lost. Under load this happened in 2 of 7 runs
  (1237 of 1251 lines). It is fixed now: the log is flushed every frame.

Observed (runs, `python tools/coop/determinism.py <replay>`, build `e8c4e6a2` plus this branch's changes):

- The script plays one replay with the state hash in fresh folders under `local/`, first once alone (`base`), then six
  runs at the same time: `base-2` (defaults again), `opengl`, `window` (1280x960, vsync off, interpolation 144 fps),
  `audio-low` and `audio-high` (fixed sample count per audio update), and `no-fma3`. Every run is compared with `base`
  on every recorded frame.
- The switches took effect: each game log shows `coop: test switch: FMA3 in the C runtime math functions 1 -> 0`,
  `... audio samples per update high 528, low 528` or `... high 560, low 560`. The `opengl` and `window` settings
  were in the run folder's `starship.cfg.json` (backend id 1; 1280x960, `gVsyncEnabled` 0, `gInterpolationFPS` 144).
  The game log does not name the backend.
- `local/replays/smoke-idle.sfcr` (1251 frames: title, intro, attract demo): 6/6 variants equal to base, 99 s.
- `local/replays/corneria.sfcr` (13545 frames: menus, all of Corneria, map): **6/6 variants equal to base on all
  13545 frames**, every replay `PASS`, 1040 s. `base` alone took 457 s. With six runs at once each took 464 to 484 s,
  and `window` took 582 s.

Derived:

- On one PC and one build, the hashed state of a full Corneria run depends only on inputs and seeds. Five things that
  differ between machines did not change it: start time, CPU load, graphics backend, window and frame pacing, and
  the audio device's timing. This includes the radio, whose logic reads audio state (`gRadioState` and
  `gRadioMsgId` are hashed).
- Turning off the runtime's FMA3 path did not change any hashed value either. A CPU without FMA3 therefore probably
  plays the same game as this one, at least for `sinf` and `cosf` in Corneria. No other CPU was involved.
- For decision 0002 this is the result option A needs on one machine. Lockstep also requires: equal gameplay CVars on
  every client, a session start after boot (#3), and no mid-level re-seed on Linux (`src/sys/sys_math.c:24`).

Guess:

- Sample count does not matter because sequences advance per audio update, not per sample
  (`src/audio/audio_synthesis.c:665-668`). Whether a voice layer still holds a note would then also follow from the
  sequence ticks. Not checked in the audio code beyond that loop.

Unknown:

- A second PC with the same build (another CPU model, another driver), the actual "two machines" half of #2.
- Other levels: only Corneria (on-rails) was played. All-range levels, the Landmaster and Blue Marine, bosses and
  Andross (`gScenery360` allocated mid-level, see #4) are not covered.
- Release builds, other compilers (GCC, Clang) and cross-platform play (Windows against Linux).
- Whether the MSVC runtime's FMA3 path gives other results than the non-FMA3 path at all, or the two agree bit for
  bit by design. Either way the run shows no difference for this replay.

## Networking

### 2026-11-05, #18 (WP-20), UDP transport

Identity: no game and no ROM involved, two or three processes on one machine, UDP over loopback. Commit `12329623` on
Linux, GCC, Debug. Commit `fdd01ee1` in CI on Linux (GCC) and Windows (MSVC), both Release.

**Observed**

- All 17 scenarios of `tools/coop/net_test.py` pass, including the `hell` profile: the relay dropped 710 of 3042
  datagrams and duplicated 120, and all 300 reliable messages per direction still arrived in order, in about 10 s.
- A 2 s outage, in either or both directions, loses no reliable message and does not end the connection.
- Timeouts fire before the limit you would expect from the outage: with a 2000 ms timeout, both sides reported it
  1.8 s after the link died, because the clock runs from the last datagram heard, up to one ping (250 ms) earlier.
- Two-way traffic hides a missing ack-only datagram. A build that never sent `ACK` passed the `lan` scenario, because
  acks ride on data and pings. Only one-way traffic shows it, so the virtual network now has that case.
- SDL2_net's shared CMake target does not pass SDL2 on to whatever links it: linking `coop_net_test` against
  `SDL2_net::SDL2_net` alone failed with `undefined reference to symbol 'SDL_GetTicks'`
  (`tools/coop/nettest/CMakeLists.txt`).
- `SDLNet_UDP_GetPeerAddress(socket, -1)` returns the port picked for `SDLNet_UDP_Open(0)`
  (`src/port/coop/net/transport_sdlnet.c`).
- All 17 scenarios pass on Windows too. The virtual network gives the same counts on GCC Debug, GCC Release and MSVC
  Release: dropped 113877, duplicated 21597, corrupted 10131.
- On Windows `stray_datagrams` counted 1954 of 2257 junk datagrams as rejected; on Linux all 3000. In the Windows run
  301 of the 2257 were larger than the 1201-byte receive buffer, which leaves 1956.
- CMake's Visual Studio generator leaves the targets of an `EXCLUDE_FROM_ALL` directory out of the solution, so
  `cmake --build --target` fails with `MSB1009: Project file does not exist`. Ninja does not mind. Put
  `EXCLUDE_FROM_ALL` on the target instead (`tools/coop/nettest/CMakeLists.txt`).

**Derived**

- A goodbye that gets through ends a connection at once. Without one, a dead peer costs the full timeout.
- Under a long one-way outage the resend count grows quickly (about 1200 resends in the 2 s outage tests, with 120
  messages sent per second in all). The backoff bounds it, but a bigger reliable lane would need real congestion control.

**Guess**

- Windows drops a datagram larger than the receive buffer inside `recvfrom`, so it never reaches the core and is not
  counted. Linux cuts it to the buffer size, and the core rejects it. Harmless either way: traffic was unaffected.

**Unknown**

- The other 2 of the 1956 junk datagrams that Windows did not count.
- Anything between two real PCs: NAT, firewalls, real loss. Every run so far is loopback through the relay.
- The longest stall of the game loop, which `timeoutMs` (5000 by default) has to outlast. Level loads may matter.
- macOS: `Brewfile` and `.github/macports.yml` list SDL2_net, but nothing was built or run there.

### 2026-11-05, #18 (WP-20), boot with networking on

Identity: ROM US 1.0 (SHA-1 `d8b1088520f7c5f81433292a9258c1184afa1457`, `config.yml:34`), commit `326b4572`, Linux,
GCC 16.2.1, Debug, graphics backend OpenGL, `USE_NETWORKING` on, `SDL_VIDEODRIVER=x11`, `SDL_AUDIODRIVER=dummy`.

**Observed**

- The game reached the title screen with no input. Sampled every 7 s: `gGameState` 2, `gTitleState` 2,
  `sCutsceneState` 1, 2, 3, 4, 5, then 0 (`TITLE_SCREEN`) in the sample at system frame 3131. It was still running at
  frame 3762, when the probe ended it. No signal stopped it except the probe's own.
- A screenshot of the game window (title "Starship (OpenGL)") showed the title screen.
- The binary has 13 `CoopNet` symbols and links SDL2_net (`ldd`). The game log has no error or critical lines.
- With `SHIP_HOME` set, no file named `starship*` or `default.sav` appeared in `$HOME/.local/share` or `$HOME/.config`.
- `gSysFrameCount` rose by about 210 every 7 s, so 30 system frames per second on the title sequence.
- SDL is the only audio backend on Linux (`libultraship/src/audio/Audio.cpp:35-39`), so `SDL_AUDIODRIVER=dummy` keeps
  a test run silent.
- With `kernel.yama.ptrace_scope` at 1, gdb cannot attach to a game started by something else. Start the game from
  gdb instead. To stop it for a sample, use `handle SIGINT stop print nopass`: `noprint` implies `nostop`, and the
  first run of this check sat in `continue` until its timeout because of that.
- EU 1.0 does not work as a base ROM. `config.yml` sends its output to `./mods/sf64eu.o2r`, so no `sf64.o2r` comes
  out and the asset step fails.

**Derived**

- Building the transport in and linking SDL2_net does not stop the game from booting on Linux.

**Unknown**

- The transport inside the running game. Nothing calls it, so this run opened no socket and never called
  `SDLNet_Init`. WP-21 is the first code to do that.
- A boot with networking on under Windows or macOS. CI builds the game on Windows but does not start it.
- Anything past the title screen.

### 2026-11-06, #92, what the sanitizer runs can fail on, and why the session tests need no log scan

Read at `797c678b` (origin/coop).

**Observed**

- `net_mutants.py --asan` has four breaks, all caught in Debug and in Release: a leak (`sim`, `lan`), a signed shift
  (`sim`, `lan`), a stack overflow by one byte in `Core_Write` (`sim`, `lan`), and a heap overflow in `Sdl_Tx`,
  `transport_sdlnet.c:34`, after the host's refusal (`host_full`).
- All four still pass every named scenario in the build without sanitizers, so no ordinary check sees them.
- With `sanitizer_report` switched off, `refuse_overruns_tx_packet` passes `net_test.py --asan` too. So that break also
  guards the scan: remove the scan and it survives.
- Every program's exit in `session_test.py` is read. Each of the 37 programs of the 11 scenarios with real processes
  was made to end with exit code 77 after its normal end, one at a time, and every run failed with that code. The
  same probe on `net_test.py` reports the host and the first client of `host_full` as not read, so it can tell. The
  only program in the session tests that nobody waits for is the relay, which is Python and has no sanitizer.
- `session_mutants.py --asan` now has `host_close_leaks` and `client_close_leaks`, which fire at the very end of one
  side, after its result file is written, and `refuse_reads_past_session`, the session's version of the `host_full`
  break. All are caught, through `run.wait` (`net_test.py:99`), with no scan.
- With `run.wait` not reading the host's exit, `host_close_leaks` passes all 11 scenarios; the same for the clients and
  `client_close_leaks`. So the exit is the only thing that fails them, and those two breaks guard it.
- Such a failure reads `host exited with 1`. What the sanitizer found is only in `host.log`, and CI keeps no logs.
- The report of `refuse_hell` is in `try0/host.log`: `damaged_runs` gives each try a subfolder
  (`session_test.py:56`), and `sanitizer_report` reads only the top of a folder (`net_test.py:359`).
- `s->text[sizeof(s->text)] = 0` in `session.c` is seen by no sanitizer, and no log held a report. In the 64-bit Linux
  build `CoopSession` is 5400 bytes and `text`, its last field, ends at 5396, so the write stays in the allocation.
- `roster_read_past_end` in `SANITIZER_BREAKS` also fails `sim` without sanitizers (`roster_trailing`), so it is not
  one that only a sanitizer can see.

**Derived**

- `session_test.py` does not need the scan to catch a finding: a finding ends its program with a code other than 0, and
  every program's exit is read. The scan would only add what was found to the summary line.
- A scan copied into `session_test.py` would have to search subfolders, or it misses every scenario with a relay.
- A new scenario that leaves a host or all clients running is caught by the two close breaks. One that leaves only some
  of several clients running is not: a break fires in all clients of a scenario, and the others still fail it.

**Unknown**

- Why UBSan's bounds check says nothing about `s->text[sizeof(s->text)]`. Guess: it treats a last array like a flexible
  one.

### 2026-11-06, #75, transport tests under sanitizers, and the audit in CI

Read at `95962d04` (origin/coop).

**Observed**

- `net_test.py --asan` found nothing in `net_core.c` or `transport_sdlnet.c`: 17/17 scenarios, in Release and in Debug,
  and no sanitizer line in any log.
- The step can fail. Throwaway breaks, each reported in the summary line: dropping `free(core)` from
  `CoopNetCore_Destroy` (LeakSanitizer, `sim` and `lan`), `1u << (i + 1)` in the ack loop (UBSan, `sim` and `lan`), a
  `memcpy` of `size + 4096` in `Sdl_Tx` (AddressSanitizer, `lan`).
- A scenario does not watch every program to its end. `sc_host_full` never looks at its host, so with a heap overflow
  in the host's refusal path the scenario itself still passed. That is why `net_test.py --asan` also reads each
  scenario's logs: the same break then fails `host_full`.
- `sp_audit.py --check` passes in a depth 1 clone without submodules: it only runs `git ls-files`.
- The audit holds line numbers. One comment line added at the top of `src/engine/fox_play.c` makes `--check` fail, so
  any change that moves a site needs a rerun of `sp_audit.py`, not only a change that adds one.

**Derived**

- Programs that `Run.stop_all` kills never reach their leak check. That covers the hold-mode host and first client in
  `host_full`, and the client that `kill_client` kills. Their leaks are only seen in the scenarios that exit normally.
- AddressSanitizer knows allocations, not fields, so an overrun from one `TxSlot` or `RxSlot` into its neighbour stays
  inside `CoopNetCore` and goes unseen. Not tested.

### 2026-11-06, #55, what the session tests can and cannot see

Read at `a69c811a` (origin/coop).

**Observed**

- Nothing could fail the hold-back checks. `Check_Refused` and `check_refused` assert that no game message crossed a
  refused handshake, but both ends only sent one after JOINED, and `CoopSession_Send` refuses before that. Both ends
  now push game messages straight into the transport (`--eager` for real processes), and a clean run must have
  dropped some.
- `CoopIdentity_Read` returns 0 on failure, and the callers compared that with the size, so an empty identity passed
  (`session.c:193, 309, 325` at `a69c811a`). The result: an empty hello got an ordinary refusal for every field instead
  of the malformed one, a one-byte welcome let the client in with an empty host identity, and a four-byte refusal
  read as a valid refusal. `Identity_ReadAll` now checks for 0 first.
- Two CVars that change game flow were not in the table. With the level selector, `gSkipBriefing` starts the level
  straight from the map instead of through the briefing (`src/mods/levelselect.c:115`; the menu entry is
  `src/port/ui/ImguiUI.cpp:816`). `gCoopDebugPlayerCount` sets how many players the level has
  (`src/port/coop/game/players.c:30`, default 1).
- On a bad link a refused client can miss the refusal. In the virtual network (35 percent loss, seed 25 of
  `Sim_Lossy`) all five copies of `SESSION_REFUSE` within the 3000 ms linger time were lost, at 180, 380, 780, 1580 and
  2580 ms into the run. The host then dropped the client, which read "The other player closed the connection."
  Nothing got through, so the rule holds, but the player gets a worse text.

**Derived**

- A check that no input can make fail looks like coverage. The mutation table (`tools/coop/session_mutants.py`) is
  there to find those, and it found a gap in `check_joined` too: the Python check did not compare the two texts of a
  warning, only the C one did.
- Eight changes to `session.c` cannot be seen from outside, in the plain or the sanitizer build; they are listed at
  the top of `session_mutants.py` with the reason. Three more only show in the sanitizer build (`--asan`): a leak, a
  read past the end of a short refusal, and an out-of-range peer index that stays inside the session struct.

**Unknown**

- Whether a player on such a link is better served by a longer linger time. It is a trade against a slot held by a
  client that already gave up.

### 2026-11-06, #22, join flow: from the lobby into a level

Identity: ROM US 1.0, on commit `d71979bf`, Linux, GCC, Debug, OpenGL on a software renderer, no window, 2 or 3 game
clients, UDP over loopback, no input after the level start. The rules and the wire form are in PROTOCOL.md, section
Level start. `python tools/coop/join_test.py` runs all of this.

**Observed**

- Real games ran the session for the first time here, and `CoopIdentity_FromGame` (`src/port/coop/session/identity_game.c`)
  with it: two and three games of one build passed the handshake.
- Two games that start from other seeds reach Corneria with the same seeds, the same team values and the same
  characters, and their state hash is equal in every region on every compared frame: 1995 frames from the title
  screen, 2001 with the host coming from the map, 2001 with three games. A client that keeps its own values differs on
  level frame 0, in `rng` and `globals`, so the check can fail.
- `gNextGameState = GSTATE_PLAY` goes to a level from any game state, as `src/overlays/ovl_i3/fox_a6.c:2927` does:
  `Game_SetGameState` (`src/engine/fox_game.c:86`) runs right after `GamePreUpdateEvent` (`:352`, `:354`). It ran from
  the title screen and from the map. `CoopLevelStart_Enter` uses it, so the host and a client take one path.
- On the frames between that and `Play_Init` the game is in `GSTATE_PLAY` but `gGameFrameCount` still counts the screen
  it came from: `Play_Init` clears it (`src/engine/fox_play.c:2879`), and `PLAY_STANDBY` then `PLAY_INIT` come first
  (`:7173`, `:7179`). In the run looked at, five rows differ in `globals` there and nothing else. The first frame in
  `PLAY_UPDATE` is where games can agree, so `join_test.py` compares from there.
- `gRadioMsgId` is set by a radio line (`src/engine/fox_radio.c:128`, `src/overlays/ovl_menu/fox_title.c:3654`), read at
  `src/engine/fox_radio.c:660`, and no level start clears it. With a host coming through the map's briefing, `globals`
  differed from a client on the title screen on level frames 0 to 331 and was equal from 332 on. With the value in the
  table the same run was equal on 2014 frames.
- A level reads the save: Start in a level intro skips the intro only if `normalClear` is set for that planet
  (`src/engine/fox_play.c:7198-7200`). No run here pressed Start, and every game had a fresh save.
- `session_mutants.py` showed that `p->state == PEER_JOINED` could not be seen in the start guard: a roster slot only
  holds value 1 for a joined peer, as `Host_Hello` sets it and `Host_Left` clears it. It is gone.
- Bit 9 of the refusal reasons was in the test constant `0x04030204`, which stood for bits nobody uses. It is
  `0x04030404` now.

**Derived**

- Every game entering from the message, the host's too, means no game's history counts. The host's own values go
  through the same path as everyone else's, so a difference cannot hide on one side.
- The values are what a level reads and level init does not set: the seeds (`Rand_Init` reads the clock at boot,
  `src/engine/fox_game.c:58`), `gTeamDamage` and `gTeamEventActorIndex` (#67), the team and Star Wolf arrays, lives,
  bombs, lasers, gold rings, `gExpertMode`, `gClearPlayerInfo` (`src/engine/fox_play.c:4925`), `gGreatFoxIntact`,
  `gVenomHardClear` and `gRadioMsgId`. The list comes from reading and from the hash, so it covers what those two can
  see.
- The roster has to be taken with the level. If it went out as it is when the send happens, a client leaving in between
  would give the host one player count and the others another.
- `Rand_ZeroOne` re-seeds from the clock when a seed is 0 (`src/sys/sys_math.c:25`), which would part two games. No guard
  is needed: 30269, 30307 and 30323 are prime, so a seed that is not 0 never becomes 0.
- Picks race the start harmlessly: the host decides by arrival, and the level start says what each client holds.

**Choices the owner can reverse** (from the coordinator's guidance and this code; not decided in a record)

- The host does not start while a client that joined holds nothing. So nobody flies with nothing, and decision 0005 does
  not have to say what such a client flies.
- A client in the handshake holds the start up too, for 10000 ms at most.
- Nobody joins once a level has started, and a client that leaves cannot come back. Late join has no design.
- There is one level start per session, and nothing opens picks again.
- A host's lives, bombs, team shields and the rest overwrite what a client has. That says nothing about lives or saves
  as game rules, only that the games start equal.

**Unknown**

- Every level but Corneria. Levels with a phase and all-range levels have not run from a level start.
- A client coming from anywhere but the title screen, for example the map, a level it plays alone or the pause menu.
- What the next level does. The session takes one level start, and how a campaign goes on is open.
- What happens in a level when a player leaves. It stays in the player count, and whether a computer wingman takes over
  is open.
- Whether a game state exists that neither the hash nor reading found, for example a `static` in an overlay.
- Windows and macOS: none of this ran there.

### 2026-11-06, #58, characters through the session

Read at `95962d04` (origin/coop). The rules and the wire form are in PROTOCOL.md, section Characters.

**Observed**

- The session carried nothing about characters (`src/port/coop/session/session.c` at `95962d04`), and nothing in the
  game calls the session yet, so all of this is reached from the test program only.
- The transport's `disconnect` raises no event (`src/port/coop/net/transport.h`, "No event follows"), so a host that
  drops a joined client itself has to report LEFT itself. `Host_Left` is the one way out of JOINED now.
- A reliable send fails when 128 messages are in flight (PROTOCOL.md). `Host_Hello` can turn that into a refusal, but a
  roster has no such way out: one that is simply lost leaves a client disagreeing for good. So what a peer is owed
  stays owed and goes out at the start of a poll (`Host_SendOwed`).
- A joined client does not read `SESSION_REFUSE` (`Client_Poll`; `Script_ClientJoined` holds it to that), so the host
  cannot tell a joined client why it drops it. Such a client sees "The other player closed the connection."
- `session_mutants.py` left out dropping `memset(p, 0, ...)` on CONNECTED as invisible. That no longer holds: the
  memset also clears a refusal still owed to the slot's last client, so it is a row in the table now.

**Derived**

- Two clients cannot disagree on who won a character, because neither decides: the host orders picks by arrival, and
  the reliable lane delivers its rosters in order. No version number is needed; the last roster is the truth.
- A roster is 4 bytes, so sending the whole of it on each change is simpler than sending changes, and a client that
  missed one behind a full window loses nothing.
- Two clients can race for a wingman, so a pick can only be refused for that with three or more players. The tests
  run three clients, which is also the most a host takes, so four players are covered and two are a smaller case.

**Choices the owner can reverse** (from the coordinator's guidance and this code; not decided in a record)

- A refused pick does not end the session; the client stays and may pick again.
- Every roster change goes to everyone, so a menu can grey out what is taken (WP-25).
- A client holds nothing until it picks. The host gives out nothing by itself.
- A client that leaves frees its character and its slot at once. Nothing is kept for its return.
- A client can change its pick to any free character, and cannot go back to holding nothing.

**Unknown**

- When picks close. Nothing stops a pick once a level runs; WP-24 has to.
- What a level start does with a client that holds nothing. Decision 0005 says a wingman no player chose stays a
  computer wingman, but not what such a client flies; WP-24 has to.
- What happens in a level when a player leaves. The character is free in the roster at once, but until WP-17 it means
  nothing in the level, and whether a computer wingman takes over is open.

### 2026-11-06, #23, the co-op menu: where it ticks, and what a window can see

Read at `797c678b` (origin/coop). The window is `src/port/coop/ui/menu_window.cpp`, the logic `src/port/coop/ui/menu.c`.

**Observed**

- `Gui::DrawMenu` calls `Update()` on every registered window each frame, and only `Draw()` looks at whether it is
  visible (`libultraship/src/window/gui/Gui.cpp`, `DrawMenu`; `GuiWindow.cpp`, `Draw`). So a window's `UpdateElement`
  runs with the window closed, and that is where the session is polled. The menu bar is different: nothing of
  `GameMenuBar` runs while it is hidden (`GuiMenuBar.cpp`, `Draw`).
- A dropped frame skips all of it: `Fast3dWindow::DrawAndRunGraphicsCommands` returns before `gui->StartDraw()` when
  `IsFrameReady` is false (`libultraship/src/graphic/Fast3D/Fast3dWindow.cpp:178`). The next frame polls what piled up,
  so `CoopMenu_Tick` has to read until `CoopSession_Poll` returns false.
- The game's frames and the gui run on one thread: `Graphics_ThreadUpdate` is called from `push_frame`, and that from
  the loop in `src/port/Game.cpp:47`; the only `std::thread` in `src/port/Engine.cpp` is the audio one (`:444`).
- The transport pings the moment a link connects (`l->nextPing = now`, `src/port/coop/net/net_core.c:235, :493`). So on
  a good link both sides already have a round trip when the welcome is through, and a player rarely sees "not measured".
  `coop_menu_test` has to hold back pongs to reach that state.
- `rttMs` is 0 until the first pong, and a measured round trip of 0 ms reads the same (`Link_OnRtt`). The menu shows
  both as not measured.
- A client only knows its round trip to the host. The roster carries no ping, so a client has no number for another
  client.
- An open popup blocks hovering in every other window (`IsWindowContentHoverable`, `imgui.cpp:4588` in the ImGui 1.91.9b
  that the build fetches), and `UIWidgets::WindowButton` leaves its menu open. The first run of `menu_test.py --game`
  stopped there: the entry stayed open after the button, and a click on its label did not close it within 60 s. The
  entry now closes itself when the button is pressed.
- `CoopIdentity_FromGame` has now run in a game: three games of one build matched, and a fourth with `gInvincible` set
  in its config was refused with `Setting "Invincible" (gInvincible): host 0, joining player 1.`

**Derived**

- The menu has one reader of session events, `Menu_OnEvent`. Whatever else needs them, WP-24 first, has to go through
  that switch or the events are gone: `CoopSession_Poll` hands each one out once.
- Ping is measured per frame on both ends, so it holds up to about two frames on top of the network (guess at the size,
  not measured).

**Choices the owner can reverse** (not decided in a record)

- The port field starts at 46464, on both sides. Nothing depends on the number, and 0 on a host picks a free port.
- The address and port are not kept between runs.
- A client is shown the three wingmen and picks with one button each. A character held by someone else is greyed out
  with that player's number.
- While a pick is under way every choice is greyed out, so nothing can overtake it.

**Unknown**

- How the window works for a person at a real screen, with a real mouse and keyboard and at other window sizes. It has
  only been seen in a capture of a game with no window, at 1024 by 768 on a software renderer.
- Whether a host name that takes long to resolve is a problem in play: `CoopNetSdl_Connect` blocks on the lookup
  (`src/port/coop/net/transport.h`), and the menu calls it on the game's thread.

### 2026-11-06, #126, starting a level from the co-op menu

Read and run at `212271f6`, on top of PR #124 and PR #125. ROM US 1.0, Linux, GCC, Debug, OpenGL on a software renderer,
no window, 2 or 3 clients over loopback. The Start button and the event reader are in `src/port/coop/ui/menu.c`, what
touches the game in `src/port/coop/ui/menu_game.c`.

**Observed**

- The window's `UpdateElement` runs inside `Graphics_PushFrame`, after `Game_Update` (`src/sys/sys_main.c:248, :264`),
  with `Audio_Update` (`:280`) and `Timer_Update` (`src/port/Game.cpp:28`) still to come in that frame.
- `GamePreUpdateEvent` is the first thing in `Game_Update`, and `Game_SetGameState` the next
  (`src/engine/fox_game.c:352, :354`).
- `GameUI::SetupGuiElements` runs before the events exist (`src/port/Engine.cpp:306, :311`,
  `src/port/mods/PortEnhancements.c:440`), and a listener on an event not registered yet throws
  (`src/port/hooks/impl/EventSystem.cpp:13`). So `CoopUi::Setup` cannot add one; `CoopUi::Listen` does, from
  `GameEngine::Create`.
- With real widgets clicked in real games, `menu_test.py`'s `game_level`: three games from other seeds, same state hash
  in every region on 1270 frames from the first frame after `Play_Init`. `game_level_map`: a host that came from the map
  (game state 4), a client from the title screen, same hash on 1238 frames.
- The control, a throwaway build whose menu enters each game with its own values: the hash logs differ on level frame 0,
  in `rng` with everyone on the title screen, and in `rng` and `globals` with a host from the map.

**Derived**

- The menu's session now runs on `GamePreUpdateEvent`, not in `UpdateElement` as in the #23 entry above. That is where
  `join_driver.c` runs it, so a level start from the menu goes in the order `join_test.py` tested: state hash, session,
  `Game_SetGameState`. `CoopLevelStart_Enter` asks for nothing to run between the values and the level, and in
  `UpdateElement` the audio and the timers would.
- A dropped frame no longer skips the session, as it still skips the window (#23 entry). The poll comes once per game
  frame now, not once per drawn frame, so a ping can hold up to one game frame more (guess at the size, not measured).

**Choices the owner can reverse** (not decided in a record)

- Start sends everyone to Corneria, phase 0 (`CoopMenuGame_Level`). The menu has no level choice: #126 asks for none,
  and only Corneria has been run.
- A host alone can press Start. The session allows it and the menu follows the session.
- The window stays open when the level starts; the player closes it.
- From the level start on a client's character buttons are gone, not grey.
- Leave in a level ends the session for that player, and the level goes on in that game.

**Unknown**

- What happens once a player touches the controls. No game here got input after its level start, and nothing keeps
  games together until WP-22.
- A level start reaching a game anywhere but the title screen or the map: in a level, paused, on the game over screen,
  in versus. Read, not run: `Play_Init` sets `gCamCount` back to 1 and `Play_Setup` clears `gVersusMode`
  (`src/engine/fox_play.c:2929, :2864`).
- Whether the default time-out of 5000 ms holds through a level load on a slow PC. The menu passes no config
  (`src/port/coop/ui/menu.c:67, :101`), and `join_driver.c:34` asks for 30000 ms on purpose. Every run here stayed in
  the session, at load averages of 50 to 90.
- A second session in one run of the game. `sim` covers the menu's side of it; no real game has done it.

### 2026-11-05, #19 (WP-21), session handshake: build, ROM, CVars

Read at `1ec6c677` (origin/coop). The handshake code is in `src/port/coop/session/`, the wire form in PROTOCOL.md.

**Observed**

- The ROM version the port knows is the game version of each archive. `Archive::Load` reads a file named `version`
  from the archive: one byte of endianness, then a u32 (`libultraship/src/resource/archive/Archive.cpp:33-41`).
  Torch writes that u32 as the ROM header CRC at offset 0x10 (`tools/Torch/src/Companion.cpp:1243-1251`,
  `tools/Torch/src/n64/Cartridge.cpp:9-10`). Only archives that have a `version` file add to the list
  (`ArchiveManager.cpp:266-268`); `starship.o2r` has none.
- An `sf64.o2r` made from US 1.0 (SHA-1 `d8b10885...`) holds `01 76 92 4C A9`, so its version is `0x76924CA9`.
- `src/port/Engine.h:7` names `0x94F1D5A7` as `SF64_VER_US`. That is not the US 1.0 value above, so
  `GameEngine_HasVersion(SF64_VER_US)` is false with a US 1.0 archive.
- The game does not know its git commit. The only version in the build is `PROJECT_VERSION` 2.0.0
  (`CMakeLists.txt:4`). WP-21 adds `tools/coop/cmake/coop_build_id.cmake`, which writes the commit into a header on
  every build.
- CVars read by game code that change game state or game flow, so they must match (the table in
  `src/port/coop/session/sim_cvars.c`):
  - Damage and shots: `gInvincible` (`src/engine/fox_play.c:977, 5629, 6951`), `gUnbreakableWings`
    (`fox_play.c:938, 955`), `gRapidFire` (`fox_play.c:3331, 3459`, `fox_beam.c:1789`, `fox_enmy.c:2849`,
    `ovl_i3/fox_aq.c:1331`), `gLtoCharge` (`fox_play.c:3334, 3373`), `gRestoreBetaBombExplosion`
    (`fox_beam.c:1320, 1350`).
  - Input: `gInvertYAxis` multiplies `gInputPress->stick_y` inside the player update (`fox_play.c:3760, 4006`,
    `fox_tank.c:385, 674`, `ovl_i3/fox_aq.c:809, 884`).
  - Rules and flow: `gHit64FalcoDead`, `gHit64SlippyDead`, `gHit64PeppyDead` (`fox_play.c:2938-2944`),
    `gHit64SelfDestruct` (`src/mods/hit64.c:7`), `gForceExpertMode` (`ovl_menu/fox_option.c:371`), `gLevelSelector`
    (`fox_message.c:53`, `ovl_menu/fox_map.c:1893, 4116, 5528, 6107, 6751`), `gSfxJukebox` (`fox_option.c:400,
    1665`), `gDebugEnding` (`ovl_menu/fox_title.c:3420`).
  - Bug fixes in level code: `gSzMissileBug` (`ovl_i4/fox_sz.c:186`), `gSzActorFix` (`fox_sz.c:1901`, changes a loop
    bound), `gMaCameraFix` (`ovl_i5/fox_ma.c:7012`, writes `gCsCamAtY`).
  - Writes actor state although it looks visual: `gTeamFaces` sets `iwork[14]` (`fox_demo.c:106`,
    `ovl_i2/fox_me.c:1920` and 13 more sites), `gDisableLOD` sets `iwork[KA_ACTOR_LOW_POLY]`
    (`ovl_i4/fox_ka.c:2642`), `gDisableGorgonFlash` decides whether `gFillScreenAlpha` is set
    (`fox_effect.c:3818`, `ovl_i3/fox_a6.c:837`).
  - Radio and voice: `gVoiceLanguage` sets an audio channel's IO (`src/audio/audio_general.c:546, 2789`); the radio
    reads audio state (`fox_radio.c:615-645`, WP-02). `gEnemyRedRadio` sets `radioRedBox` in a hook
    (`src/port/mods/PortEnhancements.c:319`).
  - Port enhancements in `src/port/mods/PortEnhancements.c`: `gInfiniteLives` (122), `gInfiniteBombs` (129),
    `gHyperLaser` (136), `gScoreEditor` and `gScoreEditValue` (143-147, write `gHitCount`), `gSpawnerMod` (167),
    `gInfiniteBoost` (196-212), `gLaserRangeMult` (218, scales a shot timer), `gRestoreBetaCoin` (227, 243),
    `gRestoreBetaBoostGauge` (287, 309, 392), and the developer tools `gDebugSpeedControl` (18), `gDebugJumpToMap`
    (39), `gDebugWarpZone` (62), `gDebugNoCollision` (88), `gDebugLevelComplete` (95), `gDebugJumpToAllRange` (112),
    `gDebugPause`, `gLToDebugPause`, `gLToFrameAdvance` (173-184).
  - Checkpoints per level: `gCheckpoint.<level>.Set`, `.gSavedGroundSurface`, `.gSavedPathProgress` (float),
    `.gSavedObjectLoadIndex`, read at level start (`fox_play.c:4709-4716`).
- Left out because they only change drawing or sound output: `gRadioCommBox.expand` (`fox_hud.c:213, 2279, 2656`,
  `fox_radio.c:133, 746-811`, all positions of draw calls), `gDisableStarsInterpolation` (`fox_bg.c:255, 346`),
  `gCockpitOpacity` (`fox_display.c:726`), the speaker positions and volumes in `src/audio/`, and the resolution,
  HUD and window CVars in `src/port/`. WP-02 found no hash difference with another interpolation rate or window size.
- The menu's "Set Checkpoint" writes `gCheckpoint.<level>.gSavedPathProgress` twice, first as an integer with
  `gGroundSurface`, then as a float (`src/port/ui/ImguiUI.cpp:896-897`). `gSavedGroundSurface` is never written, so
  the game reads its default (the current global) there. An upstream bug; harmless for the handshake, which reads
  the same on both sides.
- Test: `python tools/coop/session_test.py` gives 9/9 (Windows 11, MSVC 19.44, Debug). Three bugs put in on purpose
  (CVars not compared, refusal not held until it arrives, platform refusing) each failed `coop_session_test sim`.

**Derived**

- US 1.0 and US 1.1 should give different versions, because Torch takes the header CRC of each ROM. Only US 1.0 was
  seen.
- A CVar that is not in the table and changes the game will not be caught by the handshake, only later by state
  hashes (WP-23).

**Guess**

- `0x94F1D5A7` is US 1.1. Not checked; the text shows it as "US" together with the hex value.

**Unknown**

- Whether Windows and Linux builds compute the same game. Until it is checked, a different platform only warns.
- CVars changed after the handshake, from the menu. Nothing stops that yet; WP-22 or WP-25 should lock the table
  while a session runs.
- Mods: an `.o2r` in `mods/` with a `version` file shows up in the ROM list, one without does not, and nothing
  compares their content. `gEnhancements.Mods.AlternateAssets` (`src/port/Engine.cpp:263-469`) swaps assets; whether
  any of them change the game is not checked.
- The game glue `CoopIdentity_FromGame` (`src/port/coop/session/identity_game.c`) compiles and links into the game,
  but nothing calls it yet and it has not run. WP-24 or WP-25 will call it when a session starts.
- Builds without git get the build id `unknown`, and two of them count as the same build. Two `-dirty` builds of the
  same commit also match, whatever their changes.

## Prior work

### 2026-11-01, triage

Observed:

- No Star Fox 64 entry in the Co-op Anything directory (`data/directory.yaml`, checked 2026-11-01).
- No co-op or online fork found among the Starship forks on GitHub (forks list sorted by stars, checked 2026-11-01).
  Notable forks: `izzy2lost/Starship` (Android), `SgtBilko76/Starship-VR`, `keraion/Starship` (Archipelago).
- Star Fox 64 has a built-in 4-player versus mode; `src/engine/fox_versus.c` holds its logic.

Guess:

- The Archipelago fork (`keraion/Starship`) has client networking code that may be worth reading for how it talks to a
  server from inside the port. Not opened yet.
