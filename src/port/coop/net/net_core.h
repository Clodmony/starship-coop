#pragma once

// The connection and reliability logic of the UDP transport. It holds no sockets and reads no clock: the caller
// passes datagrams and the time in, and gets datagrams back through a callback. So the same code runs over SDL2_net
// and in the virtual network of tools/coop/nettest. See transport.h for what each operation promises.

#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CoopNetCore CoopNetCore;
typedef uint64_t CoopNetAddr; // opaque to the core, compared for equality only
typedef void (*CoopNetTxFn)(void* user, CoopNetAddr to, const uint8_t* data, uint16_t size);

// nonce tells this connection apart from an earlier one on the same address; it must not repeat.
CoopNetCore* CoopNetCore_CreateHost(const CoopNetConfig* config, CoopNetTxFn tx, void* user);
CoopNetCore* CoopNetCore_CreateClient(const CoopNetConfig* config, CoopNetTxFn tx, void* user, CoopNetAddr host,
                                      uint32_t nonce, uint32_t nowMs);
void CoopNetCore_Destroy(CoopNetCore* core);

void CoopNetCore_Receive(CoopNetCore* core, CoopNetAddr from, const uint8_t* data, uint32_t size, uint32_t nowMs);
// Resends, pings, acks and timeouts. Call it at least once per frame.
void CoopNetCore_Update(CoopNetCore* core, uint32_t nowMs);
bool CoopNetCore_Send(CoopNetCore* core, int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane,
                      uint32_t nowMs);
bool CoopNetCore_Poll(CoopNetCore* core, CoopNetEvent* ev);
void CoopNetCore_Disconnect(CoopNetCore* core, int peer);
bool CoopNetCore_Stats(const CoopNetCore* core, int peer, CoopNetStats* out);

// The checksum of the wire format, for tests that build their own datagrams.
uint32_t CoopNetCore_Crc32(const uint8_t* data, uint32_t size);

#ifdef __cplusplus
}
#endif
