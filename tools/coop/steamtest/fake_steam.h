#pragma once

// A fake of Steam's networking on a seeded virtual network: no SDK, no sockets, no real time. It keeps the rules of
// ISteamNetworkingMessages that the transport depends on, and is as unkind as those rules allow:
//
// - a stranger's messages wait until the session is accepted; sending to someone accepts them too
// - reliable messages arrive in order and once, late rather than never, until the session fails
// - unreliable messages can be lost, can overtake each other and reliable ones, and can arrive twice
// - a session that fails posts an event; a peer that closes on purpose posts nothing, the next send is just BROKEN
// - a broken session stays broken until closeSession
// - closing a session throws away what it has not delivered
//
// These come from Valve's headers and page on ISteamNetworkingMessages and from the hub's Steam page, not from a real
// run. The last one is our worst case: Valve's text says "immediately free up resources", not what happens to the data.

#include "port/coop/net/transport_steam.h"

#define FAKE_MAX_ENDPOINTS 8

typedef struct {
    uint32_t latencyMs;        // one way
    uint32_t jitterMs;         // added to the latency, at random
    double loss;               // chance of a loss in the good state of the burst model
    double burstEnter;         // good to bad, per message
    double burstExit;          // bad to good, per message
    double burstLoss;          // chance of a loss in the bad state
    double reorder;            // chance of an unreliable message skipping the latency
    double duplicate;          // chance of an unreliable message arriving twice
    uint32_t setupMs;          // how long a new session takes before anything arrives
    uint32_t sessionTimeoutMs; // Steam gives up on a dead link after this. 0: 10000
    uint32_t sendBufferBytes;  // reliable bytes on their way before BUSY. 0: 524288
    bool shutdownLosesData;    // closing the program loses what it has just sent, goodbye included
} FakeLink;

typedef struct {
    uint64_t unreliableSent;
    uint64_t unreliableDropped;
    uint64_t unreliableDuplicated;
    uint64_t reliableSent;
    uint64_t reliableResent; // virtual: costs time, nothing else
    uint64_t sessionRequests;
    uint64_t sessionFailures;
    uint64_t sessionsClosed;
    uint64_t accepts;
    uint64_t busy;
    uint64_t broken;
    uint64_t oversize; // messages over COOP_NET_MAX_DATAGRAM
    uint64_t received;
    uint64_t released;
} FakeCounters;

void FakeNet_Reset(uint64_t seed, const FakeLink* link, uint32_t startTime);
void FakeNet_Free(void);
uint32_t FakeNet_Now(void);
uint32_t FakeNet_Random(void);
const FakeCounters* FakeNet_Counters(void);

// A program on the network. An id that was killed or closed comes back as a new program.
CoopSteamBackend* FakeNet_Join(CoopSteamId id);
// Moves time on and delivers what is due.
void FakeNet_Step(uint32_t ms);
// Nothing gets from one to the other for ms.
void FakeNet_Outage(CoopSteamId from, CoopSteamId to, uint32_t ms);
// The program is gone without a word. Its transport may still be closed afterwards.
void FakeNet_Kill(CoopSteamId id);
// The session between the two fails on both sides, as a lost route would.
void FakeNet_Break(CoopSteamId a, CoopSteamId b);
// The next reliable sends of this program find Steam's queue full.
void FakeNet_BusyNext(CoopSteamId id, uint32_t sends);
// Messages that wait for this program to accept them.
uint32_t FakeNet_Held(CoopSteamId id);
