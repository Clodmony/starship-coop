#pragma once

// Hits on the players other than player 0 from things that are not effects (#136). The effects are in effects.h (#100).
//
// Upstream, these tests look at player 0 only. Each function here repeats one of them for the other co-op players and
// sits next to the upstream test, which goes on testing player 0. A parked player cannot be hit. Outside co-op these do
// nothing.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Boss;

// The test of TexturedLine_Update (lasers drawn as lines: event scripts, Titania, Andross), for the players after
// player 0, on gTexturedLines[index]. A hit uses the line up as it does for player 0.
void CoopHits_TexturedLine(int32_t index);

// The test of the beam in Bolse_8018E05C, for the players after player 0. The rest are the beam's values as that
// function works them out: x and y rotation in degrees, x and z offset from the base, and length.
void CoopHits_BolseBeam(struct Boss* base, float xRot, float yRot, float xOffset, float zOffset, float length);

// The test of Andross_8018D0D8, for the players after player 0, measured from (x, y) as that function does.
void CoopHits_AndrossReach(float x, float y);

struct Actor;

// The test of the fire of a bomb of Macbeth's train end (Macbeth_MaBombDrop_Update, state 1), for the players after
// player 0 (#272): a box around the bomb, as high as its fire has grown. The fire burns on after a hit, as it does for
// player 0, so every tank in it is hit.
void CoopHits_MacbethBombFire(struct Actor* bomb);

// The test of the blast of Macbeth's train car 7 (Macbeth_MaTrainCar7_Update, state 1, the one frame it blows up), for
// the players after player 0 (#280): a box around the car, with no mercy timer, as for player 0. The red of the
// screen stays player 0's: it is a fade every game of a session shows alike.
void CoopHits_MacbethCarBlast(struct Actor* car);

// The test of an explosion of Andross' escape (Andross_AndExplosion_Update), for the players after player 0 (#282, the
// owner's choice A): a flying player within 500 of it in x and z gets shields 0, both wings 0 and the two hits, as
// player 0 does, whatever gInvincible says, as for player 0. It then goes down as any player after player 0 does
// (decision 0007 rule 4: back beside player 0 after 150 frames for a life of the pool). The kill cutscene and the
// restart stay player 0's.
void CoopHits_AndrossExplosion(struct Actor* explosion);

#ifdef __cplusplus
}
#endif
