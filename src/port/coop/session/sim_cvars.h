#pragma once

// The CVars that change the simulation, so every player must have the same values (PROTOCOL.md rule 2). Each entry
// has a source in docs/coop/NOTES.md (WP-21). A CVar that only changes drawing or sound output is not here.
// Floats travel as their bit pattern.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Room for every entry; the table is checked against it at start (CoopSimCvars_Count).
#define COOP_SIM_CVARS_MAX 160

typedef int32_t (*CoopCvarGetIntFn)(const char* name, int32_t defaultValue);
typedef float (*CoopCvarGetFloatFn)(const char* name, float defaultValue);

uint16_t CoopSimCvars_Count(void);
// The CVar name of entry i, for example "gInvincible" or "gCheckpoint.0.Set". Returns false past the end.
bool CoopSimCvars_Name(uint16_t i, char* out, size_t cap);
// A short English label for the player, for example "Invincible".
const char* CoopSimCvars_Label(uint16_t i);
// FNV-1a over every name in order: two builds with the same table have the same hash.
uint32_t CoopSimCvars_TableHash(void);
// Fills values[0 .. Count-1]. A checkpoint that is not set reads as zeros, so a stale one does not count.
void CoopSimCvars_Read(int32_t* values, CoopCvarGetIntFn getInt, CoopCvarGetFloatFn getFloat);

#ifdef __cplusplus
}
#endif
