# 0011: Sine and cosine in a session come from the N64's own code, not the C library

- Status: Accepted
- Date: 2026-11-08, accepted by the owner 2026-11-08 (option 1 of 4)
- Issue: #186

Follows [0010](0010-cross-platform-sessions.md): it let a Windows game and a Linux game join one session and waited for
a result. The first result is below.

## Context

Observed (first Windows against Linux session, #171 and #2, 2026-11-08): build `0e5a5cf66a4e` on both, host Windows
x86-64 MSVC 1944, client Linux x86-64 GCC 16.2 (glibc 2.44), ROM US 1.0, 0 resends and 0 drops. Three attempts at
Corneria, three desyncs. `desync_report.py diff` on the host's and the client's report of each frame:

| Frame | First difference |
|---|---|
| 22 | `gPlayer[0].yBob` (+128): host `0x3e800000` = 0.25, client `0x3e7fffff` = 0.24999999. One float, one ulp. |
| 5 | `gActors[0].fwork[1]` (+404): host `0x3f2b4efe`, client `0x3f2b4efd`. One ulp; the computation is not traced. |
| 0 | `gRightWingHealth[0]`: host 60, client 40. Leftover state, not a platform difference: #187. |

Observed in the code (read at `0e5a5cf6`):

- `SIN_DEG`, `COS_DEG`, `__sinf` and `__cosf` end in the C library's `sinf` and `cosf` (`src/port/Engine.cpp:691-704`).
  `SIN_DEG` is used 186 times and `COS_DEG` 63 times in `src/engine`, `src/overlays` and `src/sys`. The libultra
  versions in `src/libultra/gu/sinf.c` and `cosf.c` are not in the build's source list (`CMakeLists.txt` names
  neither).
- Other readers of `__sinf` and `__cosf`: audio (`src/audio/audio_general.c:2264-2265`) and the projection
  (`src/libultra/gu/perspective.c:27`). Read, not run: neither writes state that the hash covers.
- No other C library math is called by the simulation: the remaining hits in `src/engine`, `src/overlays`, `src/sys`
  and `src/libultra` are `fabs` (exact) and the game's own `Math_*` functions. `sqrtf` is exactly rounded.

Verified on this PC: at frame 22, `yBob = -SIN_DEG(bobPhase) * 0.5f` (`src/engine/fox_play.c:224`), `bobPhase` is 210.0
on both sides, and the float given to `sinf` is `0x406a927f`. Its exact sine, -0.49999998509, lies halfway between the
floats -0.5 (`0xbf000000`) and -0.49999997 (`0xbeffffff`). glibc 2.44 `sinf` returns `0xbeffffff`, which gives
`yBob` = `0x3e7fffff`, the client's dumped value, bit for bit.

Observed on Windows (MSVC v143), run by the other player and reported to the owner, not seen by the agent: `math_test.py`
passes there on the same 11018 inputs, so MSVC compiles `sincos.c` to the reference's bits, as GCC does. Windows' own
`sinf` returns `0xbf000000` for `0x406a927f`, as the host's `yBob` of `0x3e800000` needed; the Windows C library differs
from the N64 algorithm on 576 sines and 582 cosines of the 11018 (glibc: 577 and 557), so the two libraries are not the
same function.

Unknown: whether frame 5 is also a sine or cosine result; whether more differences follow once this is fixed.

## Options

1. **The N64's own sine and cosine, in sessions only.** The algorithm of libultra's `__sinf` and `__cosf`: argument
   reduction and a polynomial, all in double arithmetic (`+`, `-`, `*`). The same bits on every compiler that does not
   fuse multiply-adds. Single-player keeps the C library, as upstream. Risk: both players must switch before any level.
2. **The same, always.** One code path, and what the N64 ran. Single-player on PC changes by an ulp here and there, away
   from upstream, against the rule "co-op off means upstream behaviour".
3. **A correctly rounded math library, bundled.** A new dependency on three platforms, and a choice of library, for two
   functions.
4. **Refuse Windows against Linux again** (undo 0010). Players on different systems cannot play together.

## Decision

Option 1. While a session runs, `__sinf`, `__cosf` (and so `SIN_DEG` and `COS_DEG`) return the result of the N64
algorithm; otherwise they call the C library as before. The switch goes on when the lockstep is armed
(`CoopSyncGame_Begin`, which the co-op menu and the test join driver both call, on every game, before the level is set
up) and off in `CoopSyncGame_End`, so both games switch before the first value of the level is computed, and a menu or
a single-player game never sees it. The game writes a line to the co-op log when it goes on, with the N64 sine of
`0x406a927f`: it is `0xbf000000` on every platform, so two logs side by side show that both games use it. The code is
`src/port/coop/math/`, built with `-ffp-contract=off` where the compiler has it, with constants given as bit patterns and
no pointer type punning. `Engine.cpp` changes in its two wrappers only, marked `// COOP:`.

## Consequences

Easier: sine and cosine are the same bits on Windows, Linux and macOS by construction, and a test on one PC can show it
(golden values from a reference that does not use the compiler).

Harder: a second implementation to keep right; hash logs of session replays change by an ulp wherever a sine was
rounded differently, so any stored expectation of one has to be regenerated. Single-player is untouched.

To do:

- Test first: golden values for sine and cosine over a range of angles, including 210 degrees, from a Python reference of
  the same algorithm; the compiled C must match bit for bit. `tools/coop/math_test.py`, in `test_all.py`.
- Then the two-PC test again (`TWO_PC_TEST.md`), Windows against Linux. If it parts again, the next diff names the next
  difference (frame 5 is the first candidate).
- #187 is separate: leftover state at frame 0.
