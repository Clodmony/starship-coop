# 0015: A session's level start plays the sound ahead until no audio reset is running

- Status: Accepted (2026-11-13, by the owner; built and scripted-tested in the pull request that brought this record)
- Date: 2026-11-13
- Issues: #289, #293
- Changes: nothing in an accepted record. Adds to how a session enters a level (`CoopLevelStart_Enter`, PROTOCOL.md
  "Level start" item 4, and the restart of decision 0014).

## Context

The state hash holds the voice sequence player's clock in a level (`HashVoiceClock`,
`src/port/coop/sync/state_hash.c`, #230): where it stands says on which frame the radio leaves a line. A session's level
start resets the audio heap on every game so that the clock starts over a fixed number of frames after the level start
(`CoopLevelStart_Enter`, `src/port/coop/game/level_start.c`).

Observed (NOTES.md, 2026-11-13, #289 and #293, with a trace of `gAudioResetStep`, `sAudioResetStatus` and the voice
player's `enabled` per game frame):

- The game hands the sound thread a new reset only when the running one is done: `Audio_Update` processes no sequence
  command while `Audio_HandleReset` does not say ready (`src/audio/audio_general.c`). A level start that comes during a
  reset therefore begins its own reset when the old one ends, and the voice player comes back a number of frames after
  the level start that follows how far the old reset had got.
- A reset that ended one frame before the level start still shifts the voice player by one frame against a game
  where it ended three frames before.
- Two games are not at the same point of a reset. The restart of decision 0014 follows the level start before it by a
  few frames, each game by its own count (a player who leaves at once, a client on its own save, two games that part
  at frame 0). A first level start can come during the reset of the title screen when the games run fast.
- The games then part at level frame 0 or in the first frames, in region `globals`. Two partings in a row end the
  session (`SYNC_QUICK_PARTS`).

Unknown: whether a session at 30 frames a second with people at the controls has hit this. It needs a level start
within about 15 frames of another one, or of the title screen's reset.

## Options

1. **Play the sound ahead on the frame of the level start** until no reset is running and 16 frames more have passed,
   then ask for the level's reset. Co-op code only, no upstream file changes. Costs up to 96 frames of sound rendered
   and dropped on that one frame (a few milliseconds), and what that sound would have been is not heard: the tail of
   a fade-out at a moment when the level changes anyway.
2. **Hold the level start back** until the sound is quiet, frame by frame. Nothing is rendered twice, but every caller
   of the level start (menu, join driver, the restart) has to wait with it, lockstep's start moves with it, and a
   level start becomes a state of its own with its own time-out.
3. **Take the voice clock out of the state hash** until the reset is done. The games would not part at frame 0, but
   the clocks would still differ, and the radio would step a line on different frames later (#230): the desync
   moves, it does not go.
4. **Change the audio engine** so that a reset asked for during a reset restarts its steps (`src/audio/`). An upstream
   file, and not enough: the game side holds the request back before the engine sees it. Tried first, no effect.
5. **Leave it**, and keep the scenarios away from it (slower frame limit, a wait before a restart). The defect stays
   for a real session.

## Decision

Option 1. In a session, entering a level first plays the sound ahead on that frame (the game's `Audio_Update` and the
sound thread's updates, as a frame runs them, rendered into a buffer that is dropped) until no audio reset is running
and 16 more frames of sound have passed, at most 96 frames; then the level's own reset is asked for as before.
`SF64_COOP_TEST_AUDIO_RESET_RUNS_ON=1` turns it off for a control run. Nothing changes without a session:
`CoopLevelStart_Enter` is only called for one.

## Consequences

- The voice player comes back the same number of frames after a level start whatever the sound was doing, so a
  restart right after a level start, and a level start at a high frame limit, no longer part on it.
- The session's level start now runs audio engine code from the game thread, on one frame. The sound thread is idle
  then (it works between `StartAudioFrame` and `EndAudioFrame`), and that has to stay so: a port change that lets the
  sound thread run free would need a lock here.
- The 16 frames are measured, not derived: one frame was seen to matter, and nothing beyond a few frames was. A
  longer tail (the 90 frame fade-in of the effects player, for example) is not excluded by a proof, only by the runs.
- Not checked: Windows, macOS, a second PC, a level start that comes while the map or a cutscene plays music.
