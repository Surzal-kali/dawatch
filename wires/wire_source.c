#include "wire_source.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Lane-local xorshift64* — independent of the engine's RNG; seedable so demo
 * runs are reproducible when both sides pass fixed seeds. */
static uint64_t rng_next(uint64_t *state){
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static double rng_unit(uint64_t *state){
    return (double)(rng_next(state) >> 11) / (double)(1ULL << 53);
}

/* Poisson inter-arrival, per §9: memoryless intervals, no learnable rhythm. */
static double poisson_interval_ms(double lambda_per_sec, uint64_t *rng){
    double u = rng_unit(rng);
    if(u <= 0) u = 1e-12;
    return -log(u) / lambda_per_sec * 1000.0;
}

#define SYN_PEERS 4  // 3 chatter + 1 beaconer (when enabled)

typedef struct {
    uint64_t token;
    uint16_t dst_port;
    uint8_t  proto;
    bool     is_beacon;
    uint32_t base_interval_ms;   // chatter: Poisson-drawn; beacon: exact
    uint64_t next_start_ms;
    uint64_t last_start_ms;
} SynPeer;

typedef struct {
    WireSyntheticConfig cfg;
    uint64_t rng;
    SynPeer peers[SYN_PEERS];
} SynCtx;

static void syn_next_flow(SynCtx *c, SynPeer *p, WireFlow *out){
    memset(out, 0, sizeof(*out));
    out->flow_start_ms = p->next_start_ms;
    uint64_t *rng = &c->rng;
    if(p->is_beacon){
        // threat-pattern fiction: deliberately regular cadence, tiny volume
        out->pkt_count = 2;
        out->bytes_up = 64;
        out->bytes_down = 64;
        out->flow_last_ms = out->flow_start_ms + 10;
        p->next_start_ms += p->base_interval_ms;
    }else{
        // human-ish chatter: Poisson (exponential) inter-arrivals, spread
        // sizes, both directions
        double lambda_per_sec = 1000.0 / (double)p->base_interval_ms;
        out->pkt_count = 2 + (uint32_t)(rng_next(rng) % 30);
        uint64_t up = 64 + (rng_next(rng) % 60000);
        uint64_t down = 64 + (rng_next(rng) % 200000);
        out->bytes_up = up;
        out->bytes_down = down;
        out->flow_last_ms = out->flow_start_ms + out->pkt_count * 10;
        p->last_start_ms = out->flow_start_ms;
        p->next_start_ms = out->flow_start_ms +
                           (uint64_t)poisson_interval_ms(lambda_per_sec, rng);
    }
    out->dst_port = p->dst_port;
    out->protocol_class = p->proto;
    out->peer_token = p->token;
}

static bool syn_next(WireSource *s, WireFlow *out, uint64_t now_ms){
    SynCtx *c = s->ctx;
    SynPeer *due = NULL;
    for(int i = 0; i < SYN_PEERS; i++){
        SynPeer *p = &c->peers[i];
        if(!p->token) continue;
        if(p->next_start_ms <= now_ms && (!due || p->next_start_ms < due->next_start_ms))
            due = p;
    }
    if(!due) return false;
    syn_next_flow(c, due, out);
    return true;
}

static void syn_destroy(WireSource *s){
    free(s->ctx);
    free(s);
}

static const WireSourceOps SYN_OPS = { .next = syn_next, .destroy = syn_destroy };

WireSource *wire_synthetic_source_create(const WireSyntheticConfig *cfg){
    if(!cfg) return NULL;
    WireSource *s = calloc(1, sizeof(*s));
    SynCtx *c = calloc(1, sizeof(*c));
    if(!s || !c){ free(s); free(c); return NULL; }
    s->ops = &SYN_OPS;
    s->ctx = c;
    c->cfg = *cfg;
    c->rng = cfg->seed ? cfg->seed : 0x9E3779B97F4A7C15ULL;

    // fake "real" ids — already fiction, salted tokenization still exercised
    static const char *ids[SYN_PEERS] = {
        "syn-chatter-alpha", "syn-chatter-beta",
        "syn-chatter-gamma", "syn-beacon-delta"
    };
    uint32_t ports[SYN_PEERS] = { 443, 53, 8080, cfg->beacon_port ? cfg->beacon_port : 8443 };
    uint8_t  protos[SYN_PEERS] = { WIRE_PROTO_TCP, WIRE_PROTO_UDP, WIRE_PROTO_TCP, WIRE_PROTO_TCP };

    double per_peer = cfg->chatter_flows_per_sec / 3.0;
    for(int i = 0; i < SYN_PEERS; i++){
        SynPeer *p = &c->peers[i];
        p->token = wire_peer_token(ids[i], cfg->salt);
        p->dst_port = ports[i];
        p->proto = protos[i];
        p->is_beacon = (i == 3) && cfg->beacon_interval_ms != 0;
        if(p->is_beacon){
            p->base_interval_ms = cfg->beacon_interval_ms;
        }else{
            p->base_interval_ms = (uint32_t)(1000.0 / (per_peer > 0 ? per_peer : 1.0));
        }
        p->next_start_ms = cfg->anchor_ms;  // due immediately; Poisson spacing after
    }
    return s;
}
