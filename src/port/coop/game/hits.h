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

#ifdef __cplusplus
}
#endif
