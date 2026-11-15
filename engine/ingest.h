#pragma once
#include "contracts.h"
#include "ringbuffer.h"

/*
 * Ingest path: validation, contact storage, lifetime per-lane counters, and
 * the canary split (lane-canary contacts from an external source route to
 * ROUTE_ALERT directly — zero-FP lane, main README §6 — and never feed the
 * fused K-sigma scalar).
 *
 * Counters are lifetime totals bumped in the push path; deltas are NEVER
 * derived from ring occupancy (rb_count wraps at saturation — engine.README
 * matrix). Memory stays O(window): lane ring buffers evict at capacity.
 */

typedef struct {
    uint64_t ingress[LANE_COUNT];   // lifetime contacts accepted per lane
    uint64_t malformed;             // rejected: bad lane, payload_len > bound
    uint64_t dropped;               // contacts lost: rb_push failed
    uint64_t canary_alerts;         // lifetime external canary touches routed
} IngestCounters;

/* Contract validation: lane in range, payload_len bounded by the payload
 * (readers slice to payload_len; anything beyond 128 is corrupt traffic). */
bool ingest_validate(const ContactReport *cr);

/* Push to the lane ring buffer and bump lifetime counters. Returns the route
 * decision for this contact: ROUTE_ALERT only for an external canary touch
 * (the caller emits the Route); ROUTE_CONTINUE otherwise. Never returns an
 * alert for engine-generated (internal) canary liveness. */
RouteKind ingest_contact(RingBuffer **lane_buffers, IngestCounters *stats,
                         const ContactReport *cr, bool external);

/* Read exactly one full ContactReport from a stream socket (loops on partial
 * reads, retries EINTR). Returns 1 report read, 0 clean EOF before any byte,
 * -1 malformed/short stream or bad args. Fixed-size framing means callers may
 * loop this to drain multiple reports per connection. */
int ingest_read_contact(int fd, ContactReport *out);
