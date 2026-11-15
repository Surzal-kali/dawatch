#pragma once
#include "wire_flow.h"
#include <stdlib.h>  // free is called by the inline destroy wrapper

/*
 * Wire lane collector interface — where flow features come from.
 *
 * A source pulls one WireFlow (feature record) at a time; whatever reduces
 * live traffic to features lives behind this: synthetic feeds for demos and
 * tests today, real taps (cheap /proc-class passes, then pcap/eBPF-class
 * engines) as additional backends later. Backends reduce and discard — raw
 * packets never cross this interface, never enter lane buffers, and are
 * never a type this header can express.
 */

typedef struct WireSource WireSource;

typedef struct {
    /* Fill *out with the next flow feature whose start is <= now_ms, if any.
     * Returns false when nothing is ready at this instant (sources are
     * non-blocking). now_ms lets sources pace against the caller's clock. */
    bool (*next)(WireSource *s, WireFlow *out, uint64_t now_ms);
    void (*destroy)(WireSource *s);
} WireSourceOps;

struct WireSource {
    const WireSourceOps *ops;
    void *ctx;
};

static inline bool wire_source_next(WireSource *s, WireFlow *out, uint64_t now_ms){
    return s && s->ops && s->ops->next && s->ops->next(s, out, now_ms);
}

static inline void wire_source_destroy(WireSource *s){
    if(s && s->ops && s->ops->destroy) s->ops->destroy(s);
    else free(s);
}

/* ---- Synthetic feed (demo/test backend) ----
 *
 * Poisson chatter peers plus one optional fixed-interval beaconer. The
 * chatter tempo is Poisson because every tempo we generate must be (§9 —
 * synthetic liveness, sampling). The beaconer's interval is deliberately
 * regular: it simulates adversary behavior for the cadence detector to catch,
 * not a system tempo we run on. Peer "ids" are fake strings → salted tokens. */

typedef struct {
    double chatter_flows_per_sec;   // aggregate across normal peers
    uint32_t beacon_interval_ms;    // 0 = no beaconer profile
    uint32_t beacon_port;
    uint64_t salt;                  // deployment-secret salt in production
    uint64_t seed;                  // lane-local RNG (independent of engine)
    uint64_t anchor_ms;             // virtual-timeline origin (caller's now)
} WireSyntheticConfig;

WireSource *wire_synthetic_source_create(const WireSyntheticConfig *cfg);
