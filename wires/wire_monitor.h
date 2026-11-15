#pragma once
#include "wire_flow.h"
#include "../engine/ringbuffer.h"

/*
 * Wire lane monitor — sensor-local processing over flow features.
 *
 * Bounded by window, never by traffic (main README §5/§9):
 *   - flow ring: O(window) via the shared RingBuffer (evicts at capacity)
 *   - peer table: fixed capacity, round-robin eviction; it carries cadence
 *     state and new-peer membership, so a NEW_PEER sighting means "first seen
 *     within peer-table memory", not forever
 *   - window accumulators: scalars reset each tick
 *
 * Cadence detection (the classic wire C2 tell): per-peer ring of the last
 * WIRE_CADENCE_K inter-arrival times; when the series is long enough and its
 * relative jitter (std/mean) stays under WIRE_BEACON_JITTER_PPM, the peer is
 * a beacon suspect. The contact fires once per regularity episode — renewed
 * irregularity re-arms it, so a persistent beaconer does not spam the monitor.
 */

#define WIRE_FLOW_CAPACITY_DEFAULT 512
#define WIRE_PEER_CAPACITY_DEFAULT 128
#define WIRE_CADENCE_K 8                /* retained inter-arrivals per peer  */
#define WIRE_BEACON_MIN_INTERVALS 6     /* evaluate from this many upward    */
#define WIRE_BEACON_JITTER_PPM 100000   /* 10% relative jitter ceiling       */
#define WIRE_BEACON_MEAN_MS_MIN 50      /* plausible periodicity band        */
#define WIRE_BEACON_MEAN_MS_MAX 3600000 /* and don't call 1/hr chatter a beacon */

typedef struct WireMonitor WireMonitor;

/* Contacts surface through this callback (NEW_PEER / BEACON_SUSPECT from
 * push, FLOW_TICK from window_tick). */
typedef void (*wire_contact_fn)(const ContactReport *cr, void *user);

WireMonitor *wire_monitor_create(size_t flow_capacity, size_t peer_capacity,
                                 wire_contact_fn cb, void *user);
void wire_monitor_destroy(WireMonitor *m);

/* Ingest one flow feature: buffers it, updates window accumulators and peer
 * cadence state, fires contacts through the callback. Flow arrivals are
 * expected roughly ordered by flow_start_ms; out-of-order timestamps only
 * skip the tempo delta, they never crash or corrupt state. */
void wire_monitor_push(WireMonitor *m, const WireFlow *f);

/* Close the current window: emits one FLOW_TICK aggregate contact if any
 * traffic flowed, then resets window accumulators. Returns whether a contact
 * was emitted. */
bool wire_monitor_window_tick(WireMonitor *m, uint64_t now_ms);

size_t wire_monitor_flow_count(const WireMonitor *m);  /* current ring occupancy */
size_t wire_monitor_peer_count(const WireMonitor *m);  /* live peer-table entries */
