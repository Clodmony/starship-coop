#pragma once

// Forces Macbeth's bomb fire, beams, turret shots and car blast onto one tank and logs who they hit, for unattended
// tests (#272, #280).
//
// Off unless the environment variable SF64_COOP_MABOMB_PROBE names a file. In Macbeth, while every tank drives, the
// probe takes the cases of sMaBombCases in mabomb_probe.c one at a time, each for every player in turn: it waits until
// the target is far enough across the track from every other tank, places the hazard where the target will be, and
// lets the game run it. Only the spawn is forced; the hazard's own update does the test, as in play:
//
//   bomb  a bomb of the train's end (Macbeth_MaBombDrop_Setup with the "explode at once" velocity the beams use), a
//         few frames ahead of the target, so the target drives into its fire (Macbeth_MaBombDrop_Update, state 1);
//   beam  a beam of the train's end's state 16 (OBJ_EFFECT_380) on the target's next place, at rest
//         (Macbeth_Effect380_Update);
//   shot  a shot of a train car's turret (OBJ_EFFECT_378, Macbeth_Effect378_Update) on the target's next place, at
//         rest, 20 over it so that it is over the ground (#280);
//   roll  the same shot while the probe holds the target's barrelRollAlpha at 180, which a tank never has in play:
//         the shot's test then deflects it (gives it a velocity) and does not hit (#280);
//   blast a train car in the state it blows up in (OBJ_ACTOR_MA_TRAIN_CAR_7, Macbeth_MaTrainCar7_Update state 1), 700
//         across the track from the target on the side away from the other tanks, which are at least 850 from it: the
//         blast reaches 800. The car tests once; the probe then takes it out, with its flash and shake (#280).
//
// Every frame the hazard updates, the probe first clears every player's mercyTimer, so that an earlier hit cannot hide
// it, and sets the hazard's damage to a value nothing else in the level deals (MABOMB_MARK_*). A player counts as hit
// by the hazard on a frame that ends with hitTimer 20 (Player_ApplyDamage on this frame), hitDirection 0 (the test of
// an effect or of the fire, not a tank's own hitbox collision) and dmgType at the mark. A case ends at the target's
// first hit, at the end of its window, or when the hazard is gone; then the hazard is taken out. One line per case:
//
//   mabomb frame=<gGameFrameCount> case=<bomb|beam> target=<player> hit=<mask> first=<frames per player> lost=<0|1>
//   dx=<smallest x gap from the target to another tank> defl=<0|1>
//
// (all on one line) where hit has bit i set when the hazard hit player i, first is the frames from the hazard's first
// update to player i's hit (-1 for none), lost is 1 when the hazard was gone before the target was hit and before
// its window ended (a level restart, a cull), and defl is 1 when a shot got a velocity (a deflect; that ends the case
// too). A blast's mark is its own damage, 60. While a case cannot start, every 100 frames:
//
//   mabomb wait frame=<...> case=<...> target=<player> drives=<every tank drives> jump=<the target did not drive on
//   this frame> front=<the target is in front of player 0's camera> slot=<free slot of the hazard's array, -1 for
//   none> players=<state>:<x>:<trueZpos>,... (one per player) The last line is "mabomb done cases=<n>". The probe
//   changes the game, so
// a run with it says nothing about any other run; every game of a session must get it, and then they stay one game.

#ifdef __cplusplus
extern "C" {
#endif

void CoopMaBombProbe_Init(void);

#ifdef __cplusplus
}
#endif
