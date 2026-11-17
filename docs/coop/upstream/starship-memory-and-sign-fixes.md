# Starship: reads outside arrays, sign tests on the unsigned `iwork`, and three more port faults: draft offers

Drafts for the owner to send, or not (#262, first block, last item). **Nothing here has gone upstream.** The owner's
word so far: "not now" (2026-11-12), then "I don't care about upstream at this point" (2026-11-14): the offer comes back
only when the owner raises it. This file keeps the texts ready for that day, so the work is not done twice.

Line numbers under "Upstream" are those of `HarbourMasters/Starship` `3322643f` (2026-09-15), the commit this
repository's `main` mirrors; each was looked up there on 2026-11-16 with `git grep` on `origin/main`. Recheck them if
this waits: upstream moves. "Here" names the commit of this fork that holds the fix.

## What Starship asks for

Read on 2026-11-16 in the mirror (`origin/main`, `3322643f`), nothing on the web:

- The repository holds no `CONTRIBUTING.md`, no issue or pull request template and no code of conduct file
  (`git ls-tree -r origin/main`: `.github/` has only workflows and `macports.yml`, `docs/` only `BUILDING.md` and two
  images). Its `README.md` points to a website and a Discord server and credits contributors by name.
- **Unknown, to read before anything is sent** (`AGENTS.md` rule 11, `docs/coop/MAINTAINING.md` "Sending fixes
  upstream"): whether upstream states a policy on AI-written contributions anywhere else (the repository's GitHub
  pages, pinned issues, the Discord server), what its maintainers ask of a pull request, and whether any of the faults
  below is fixed or reported there since `3322643f`. This session did not read the web: `AGENTS.md` rule 13 keeps
  that in a session that cannot write to GitHub.
- libultraship's own rules were read for #117 (`libultraship-crash-handler.md` in this folder); none of the items
  below is in libultraship.

Route, from `docs/coop/MAINTAINING.md`: a branch from `origin/main`, the fix cherry-picked or written again without the
`// COOP:` comments and without the co-op gate, a pull request against `HarbourMasters/Starship` from the owner's
account, with the AI use disclosed. One pull request per item below keeps each small enough to read; item 1 can also
go as an issue first.

What ran for all of them: ROM US 1.0, Linux x86-64, GCC, Debug, OpenGL on a software renderer, no window. **Not run:**
Windows, MSVC, macOS, Switch, a Release build of the sanitizer runs, anything on upstream's `main` itself (the faults
were found on this fork, whose game code is `3322643f` plus the changes marked `// COOP:`).

---

## Item 1: reads and writes outside arrays

**Title:** Out-of-bounds reads and one out-of-bounds write in game code (found with AddressSanitizer and UBSan)

**Text to send**

A sweep of all 17 campaign levels under AddressSanitizer and UBSan, and one cross-platform session that went out of
sync, found the places below where game code reads or writes outside an array. On the N64 each read lands on the next
table of the ROM, which is why the game behaves; in the port it lands on whatever the compiler placed there, so the
result differs between compilers and can change between builds.

This report and the fixes were prepared with AI help (Claude Code): the test runs, the code reading and this text.

| # | Upstream place | What happens | What the N64 read | Proposed fix |
|---|---|---|---|---|
| 1 | `Aquas_Actor255_Update`, `src/overlays/ovl_i3/fox_aq.c:1791`; the same test in `Aquas_Actor255_Draw`, `:1874` | `if (var_v0 < this->iwork[0] * 50)`: `iwork` is `uintptr_t` in the port (`s32` on the N64), so a negative `var_v0` compares as a huge number, the wrap is skipped and `D_i3_801C27C8` is indexed with up to -30. For the eel of pod 0 the place of its smoke and of its drawn pieces comes from before the array. | the index is wrapped, the read stays inside | compare as `s32`: `var_v0 < (s32) this->iwork[0] * 50` |
| 2 | `Map_PathLineBox_Draw`, `src/overlays/ovl_menu/fox_map.c:5642` | `sPaths[pathId].type` with `pathId` 24, one past the table, for the last mission flown while the next planet is not chosen (`Map_GetPathId` returns 24 for no path) | `D_menu_801AFFB8[0]`, 90.0f read as an integer: none of the types, the line stays white | read the type only for an id inside the table |
| 3 | `Aquas_AqJellyfish_Update`, `fox_aq.c:5988` | `D_i3_801C0440[it0]` with `it0` 24, one past its 24 entries, while a counter runs from 106 to 119 | `D_i3_801C04A0[0]`, 15: never equal | compare only for an index inside the table |
| 4 | `Venom1_Ve1Golemech_Update`, `src/overlays/ovl_i1/fox_ve1.c:1262` (and `:858`) | `D_i1_8019AE00[this->swork[21]]`, a table of 4 (`:824`); `swork[21]` counts to 5 | the floats of `D_i1_8019AE20`: 258.0f and 0.0f | six entries, the last two those values |
| 5 | `Katina_SFTeamFleeUpdate`, `src/overlays/ovl_i4/fox_ka.c:1736-1740` (`D_i4_8019F29C`, `D_i4_8019F2AC`, `D_i4_8019F2BC` and `D_i4_8019F2CC`, `:66-69`, read with `idx + 1`) | tables of 4 read at index 4 for Peppy (`idx` 3): position and yaw of a cutscene ship | the first float of the next table: 0, 1000, -1000, 180 | a fifth entry in each table |
| 6 | `Macbeth_Texture_Scroll2`, `src/overlays/ovl_i5/fox_ma.c:470` | `for (j = arg2; j > 0; j--)` starts one row past the texture: 8 bytes are **written** past a 64-byte heap block on every call. Your comment at the caller says `LTODO: ASAN accuses Heap overflow` (`:3896`); with AddressSanitizer's allocator the game dies of it in Macbeth. | the 8 bytes after the texture in its segment | start at the last row (`j = arg2 - 1`) |
| 7 | `Map_Prologue_Update`, `fox_map.c:2123` | `D_menu_801B6934[sPrologueTexIdx]`, a table of 6, read at index 6 for the last text; it sets a fade nothing draws | - | stop at the table's end |
| 8 | `aADPCMdecImpl`, `src/audio/mixer.c:299` | `_mm_loadl_epi64` loads 8 bytes from `lower_2bit`, a table of two `uint16_t` (`:280`): 4 bytes past it | - | four entries, as `lower_4bit` has |

Listed, not changed here, in case you want them: `Aquas_BlueMarineShoot` (`fox_aq.c:1262`) reads
`gPlayerShots[D_i3_801C4190[5] - 1]` with index -1 before it tests `D_i3_801C4190[5] != 0` (swap the operands);
`Macbeth_Texture_Scroll` (`fox_ma.c:450`, under your `LTODO: this is causing corruption, overflow`) reads rows 16 and
17 of a 16-row texture and then overwrites what it copied; `Audio_UpdatePlayerReverb` (`src/audio/audio_general.c:2159`)
loops over four players whatever `gCamCount` is, so with one player it reads level memory behind the player array.

How it was found: `-fsanitize=address,undefined,float-cast-overflow` with recovery on, one game per level flown by a
scripted pilot to level complete; and for row 1 a session between a Windows (MSVC) and a Linux (GCC) build that parted
at the Aquas boss, where the two games read different memory before `D_i3_801C27C8`.

**For the owner, not to send:** rows 1 to 6 are `d50fa1c5` and `a7e3f2c2` here, row 7 `bd2121f2`, row 8 `b1f1cff1`.
Evidence: `docs/coop/NOTES.md`, "2026-11-11, the Aquas boss desync", "2026-11-12, the sanitizer sweep" (the table (a)
and list (b)), "2026-11-10, #145". Venom 1's pillar tables (`fox_ve1.c:110`, `D_i1_80199CD0[5][5]` read at `[row][5]`, #276) are a ninth place
of the same kind; its N64 values are in this fork's `fox_ve1.c` comments and it was never seen to leave the table in a
flight, so it is left out of the text.

---

## Item 2: sign tests on the port's unsigned `iwork`

**Title:** `Actor.iwork` is unsigned in the port: tests for a negative value never fire

**Text to send**

`Actor.iwork` is `uintptr_t iwork[25]` in the port (`include/sf64object.h`), an `s32` on the N64. Game code that
stores a negative number there and tests it with `< 0`, or multiplies by it, behaves differently from the N64 on every
64-bit build. None of these differs between two 64-bit builds; they differ from the original game.

This report was prepared with AI help (Claude Code).

| Upstream place | On the N64 | In the port | Seen by a player |
|---|---|---|---|
| `src/engine/fox_360.c:2315`, the ring of an all-range ship's barrel roll (`iwork[15]` is 1 or -1) | the ring turns the other way for a roll to that side | -1 is a factor of about 1.8e19 and the flip never runs | yes: the ring jumps (Star Wolf in Venom 2, the team's Arwings) |
| `src/overlays/ovl_i3/fox_a6.c:2782`, `:2786`, Area 6's explosion light (`iwork[3]`, `iwork[7]` counted below 0) | the alpha is held at 0 | the test never fires and the alpha wraps | yes, for a few frames |
| `src/engine/fox_demo.c:2403`, Fortuna's explosion (`iwork[0] < 0`) | the glow fades out | never true; in practice a port fix frees the actor one frame earlier (`fox_demo.c:2601-2604`) | no |
| `src/overlays/ovl_i6/fox_sy.c:3668`, Sector Y's robots (`iwork[19] < 0`) | four placed robots fire straight ahead | they fire the spread shot | yes: it changes how they shoot |
| `src/overlays/ovl_i3/fox_aq.c:4242`, the anglerfish's light: a negative float stored into `iwork[12]` | negative, then clamped to 30 by the next line | x86-64 wraps it to a huge value, ARM saturates to 0: the two differ | the light's alpha |

Proposed fix: cast to `s32` at each test (`(s32) this->iwork[15] < 0`), and `this->iwork[12] = (s32) (...)` for the
last row. A wider fix is to make `iwork` signed (`intptr_t`), which keeps pointers stored there working and makes
every such test read as on the N64; we did not try that.

Four more tests of the same form were read and do not differ from the N64, because the value never passes below 0
there: `src/overlays/ovl_ending/fox_end1.c`, `Aquas_Actor255_Update`'s `iwork[1]`, a fish group's `iwork[0]`
(`fox_aq.c:6485`) and `Macbeth_MaRailroadSwitch_Update`.

**For the owner, not to send:** the four that do not differ are at `fox_end1.c:1737`, `fox_aq.c:1807`, `:6515` and
`fox_ma.c:3158` in this fork; only `fox_aq.c:6485` was looked up again at `3322643f`. Here the first three rows act in a
session only (`src/port/coop/game/fixes.c`, `76c7f571`, PR #369), because outside co-op this fork keeps upstream's
behaviour; the anglerfish cast is in every mode (`8000bb37`). Sector Y's robots are **not** changed here: it would
change co-op play that #273 built, and it is the owner's open choice (#262, hand-off of 2026-11-16). If that row goes
upstream, say that it changes the fight.
Evidence: NOTES.md, "2026-11-16, #262: the sign tests", the table in "2026-11-12, the sanitizer sweep" (c).

---

## Item 3: an uninitialised local in `Effect_Effect395_Update`

**Title:** `Effect_Effect395_Update`: `sp84` is read uninitialised when the effect is killed in state 0 or 2

**Text to send**

`Effect_Effect395_Update` (`src/engine/fox_effect.c`) declares `f32 sp84;` (`:3527`) and sets it in states 0, 2 and 3.
In state 0, and on the early exit of state 2, the code calls `Object_Kill(...)` and breaks without setting it; the
test at the end of the function (`state < 4 && state != 1`) then passes it to `func_effect_8007A774` as the size of
the hit box. The size is whatever the stack held. With a large value the dead effect hits the player once.

Seen on Aquas: four games of one build, same inputs, two applied a hit of 40 to the player on one frame and two did
not; the traced box sizes were 0, 2.7e32, -104925 and -7.5e30. Fix: `f32 sp84 = 0.0f;` (a killed effect hits nobody).

This report was prepared with AI help (Claude Code).

**For the owner, not to send:** `d0407439` here (#366, PR #367). Evidence: NOTES.md, "2026-11-16, issue #366". A
compile of `src/engine` and `src/overlays` with `-O2 -Wmaybe-uninitialized` would list more of the kind; not run.

---

## Item 4: three small ones

**Title:** Three small port faults: a `bool` defined where the header says `s32`, the re-seed test in `Rand_ZeroOne`, a
level script that branches past its end

**Text to send**

1. `D_ctx_8017782C` is declared `extern s32` (`include/sf64context.h:12`) and defined `bool`
   (`src/engine/fox_context.c:10`). `bool` is one byte in the port; its users write and read four. It works while
   padding follows. Fix: define it `s32`.
2. `Rand_ZeroOne` (`src/sys/sys_math.c:25`): `if ((sRandSeed1 == 0) || (sRandSeed2 == 0) || (sRandSeed2 == 0))` tests
   seed 2 twice and seed 3 never. Probably meant `sRandSeed3`.
3. Venom 1's event script 40 loops to command 21 of its 21 commands (0 to 20) when Falco is not active. On the N64 a
   level's scripts are one run of commands, so the actor runs on into the next script; in the port each script is its
   own buffer (`src/port/resource/type/Script.cpp`) and the interpreter (`EVOP_LOOP`, `src/engine/fox_enmy2.c`) reads
   past it. We join a level's scripts in ROM order into one block in the script importer, with `EVOP_STOP_SCRIPT`
   commands after the last. Area 6's script 26 has a trigger with target 59 of 0 to 58, which is never taken.

This report was prepared with AI help (Claude Code).

**For the owner, not to send:** 1 is `bd2121f2` here; 2 is **not** changed here (co-op off keeps upstream's
behaviour, the owner's call of 2026-11-10, and a session never runs that line); 3 is `2676b8ef`
(`src/port/resource/importers/ScriptFactory.cpp`). What the N64 does at 3 is derived from Torch's layout, not seen on
hardware. Evidence: NOTES.md, "2026-11-10, #145" and "2026-11-10, #112".

---

## Item 5: frame interpolation leaves its matrix stack

**Title:** Frame interpolation: `Matrix_Pop` below the bottom of `sInterpolationMatrixStack`, and skip regions that turn
recording on

**Text to send**

1. On a screen's first frame the interpolated replay can pop the interpolation matrix stack below its first entry:
   `Option_DrawMenuCard` records its Push in the parent path and its Pop in the child, and a child that replays
   against itself runs the Pop without the Push. Each interpolated draw goes one slot lower, and the next
   `Matrix_RotateX` writes there. In a GCC Release build the memory before `sInterpolationMatrixStack` holds the RNG
   seeds: a hardware watchpoint showed `Matrix_RotateX`, called from `interpolate_branch`, writing `-0.0f` over
   `sRandSeed2` and `sRandSeed3` at 60 fps and above, after which `Rand_ZeroOne` re-seeded from the clock. Over a
   whole on-rails flight the stack pointer went as low as slot -94 at 60 fps and -282 at 120 fps. We keep the
   interpolation inside its stack.
2. `FrameInterpolation_ShouldInterpolateFrame(true)` turns recording on whatever it was before. A `(false) ... (true)`
   pair inside a frame that records nothing (a camera jump, a pause), or inside another pair, turns it on in the
   middle of the frame; a `CloseChild` then pops a path it did not open, and `current_path` underflows: a crash in
   `FrameInterpolation_RecordOpenChild` (`back()` on an empty vector) or in `Path::~Path()`. We count the depth of the
   pairs, give back the state the outermost found, and never pop the root.

This report was prepared with AI help (Claude Code).

**For the owner, not to send:** 1 is `df1f7d58` here (#141), 2 is `a9dd630d` and `b2ff5c27` (#153, #169), all in
`src/port/interpolation/FrameInterpolation.cpp` (and `include/sf64math.h:70` for the stack's size). The line numbers
of these two were **not** looked up at `3322643f` on 2026-11-16; the NOTES entries hold them as of their own commits
("2026-11-07, #141", "2026-11-10, #153", "2026-11-08, #169"). The watchpoint and the slot counts are from the #141
entry, Linux, GCC, Release.

---

## Not offered

- Everything this fork does in a session only: the sine and arctangent of the N64's own code (decision 0011), the
  written order of two random draws in one expression (#199, #209), a textured line cleared when it is taken, the
  remembered beam slots of the Garoa and the Gorgon. They matter when two builds must compute the same game, which
  upstream's single-player port does not ask for. If upstream ever wants netplay, NOTES.md has each with its evidence.
- The audio loader's read past some samples (`AudioSynth_ProcessNote`, NOTES.md "2026-11-13, #145, #232, #276"): this
  fork pads the samples in its importer; what upstream would prefer there is their call, and the text for it would need
  the measurement of that entry.

## Owner tasks before any of this is sent

1. Say that it is wanted at all (the answer of 2026-11-14 stands until then).
2. Read upstream's current rules where they are (the repository's GitHub pages, pinned issues, Discord), or have a
   session without GitHub write access read them and bring back the quotes.
3. Check each item against upstream's current `main`: fixed already, moved, or reported by someone else.
4. Decide Sector Y's robots (item 2) first.
