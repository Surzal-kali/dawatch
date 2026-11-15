#include "wire_flow.h"
#include <string.h>

static void put_u64(uint8_t *p, uint64_t v){ memcpy(p, &v, 8); }
static void put_u32(uint8_t *p, uint32_t v){ memcpy(p, &v, 4); }
static void put_u16(uint8_t *p, uint16_t v){ memcpy(p, &v, 2); }
static void put_u8 (uint8_t *p, uint8_t  v){ memcpy(p, &v, 1); }

static uint64_t get_u64(const uint8_t *p){ uint64_t v; memcpy(&v, p, 8); return v; }
static uint32_t get_u32(const uint8_t *p){ uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t get_u16(const uint8_t *p){ uint16_t v; memcpy(&v, p, 2); return v; }
static uint8_t  get_u8 (const uint8_t *p){ uint8_t  v; memcpy(&v, p, 1); return v; }

static double get_f64(const uint8_t *p){ double d; memcpy(&d, p, 8); return d; }

static void contact_base(ContactReport *r, uint8_t kind, uint64_t ts_ms){
    // explicit field init — payload bytes stay untouched so tests can prove
    // packers never write beyond payload_len
    r->ts_ms = ts_ms;
    r->lane = LANE_WIRE;
    r->kind = kind;
    r->reserved[0] = 0;
    r->reserved[1] = 0;
    r->payload_len = 0;
}

uint64_t wire_peer_token(const char *real_id, uint64_t salt){
    if(!real_id) return 0;  // 0 = untracked peer
    // FNV-1a keyed by salt: same id -> same token; token reveals nothing
    // without the salt (production MUST use a deployment-secret salt).
    uint64_t h = 0xcbf29ce484222325ULL ^ salt;
    for(const unsigned char *p = (const unsigned char *)real_id; *p; p++){
        h ^= *p;
        h *= 0x100000001b3ULL;
    }
    return h;
}

/* Layouts: u64 fields at multiples of 8, u32 at 4, u16 at 2 — no implicit
 * padding anywhere, payload_len is the exact packed size. */

void wire_pack_flow_tick(ContactReport *r, uint64_t ts_ms, const WireTickStats *st){
    contact_base(r, WIRE_KIND_FLOW_TICK, ts_ms);
    uint8_t *p = r->payload;
    {
        double d = st ? st->tempo_mean_ms : 0.0;
        memcpy(p + 0, &d, 8);
        d = st ? st->tempo_std_ms : 0.0;
        memcpy(p + 8, &d, 8);
    }
    put_u32(p + 16, st ? st->flow_count : 0);
    put_u32(p + 20, 0);                    // explicit pad
    put_u64(p + 24, st ? st->bytes_up : 0);
    put_u64(p + 32, st ? st->bytes_down : 0);
    r->payload_len = 40;
}

void wire_unpack_flow_tick(const ContactReport *r, WireTickStats *st){
    if(!r || !st) return;
    const uint8_t *p = r->payload;
    st->tempo_mean_ms = get_f64(p + 0);
    st->tempo_std_ms  = get_f64(p + 8);
    st->flow_count    = get_u32(p + 16);
    st->bytes_up      = get_u64(p + 24);
    st->bytes_down    = get_u64(p + 32);
}

void wire_pack_beacon(ContactReport *r, uint64_t ts_ms, const WireBeaconStats *st){
    contact_base(r, WIRE_KIND_BEACON_SUSPECT, ts_ms);
    uint8_t *p = r->payload;
    WireBeaconStats z = {0};
    if(st) z = *st;
    put_u64(p + 0,  z.peer_token);
    put_u64(p + 8,  z.first_seen_ms);
    put_u64(p + 16, z.last_seen_ms);
    put_u32(p + 24, z.interval_mean_ms);
    put_u32(p + 28, z.jitter_ppm);
    put_u16(p + 32, z.occurrences);
    put_u16(p + 34, 0);                    // explicit pad
    r->payload_len = 36;
}

void wire_unpack_beacon(const ContactReport *r, WireBeaconStats *st){
    if(!r || !st) return;
    const uint8_t *p = r->payload;
    st->peer_token       = get_u64(p + 0);
    st->first_seen_ms    = get_u64(p + 8);
    st->last_seen_ms     = get_u64(p + 16);
    st->interval_mean_ms = get_u32(p + 24);
    st->jitter_ppm       = get_u32(p + 28);
    st->occurrences      = get_u16(p + 32);
}

void wire_pack_new_peer(ContactReport *r, uint64_t ts_ms, const WirePeerStats *st){
    contact_base(r, WIRE_KIND_NEW_PEER, ts_ms);
    uint8_t *p = r->payload;
    WirePeerStats z = {0};
    if(st) z = *st;
    put_u64(p + 0, z.peer_token);
    put_u64(p + 8, z.first_seen_ms);
    put_u16(p + 16, z.dst_port);
    put_u8 (p + 18, z.protocol_class);
    put_u8 (p + 19, 0);                    // explicit pad
    r->payload_len = 20;
}

void wire_unpack_new_peer(const ContactReport *r, WirePeerStats *st){
    if(!r || !st) return;
    const uint8_t *p = r->payload;
    st->peer_token      = get_u64(p + 0);
    st->first_seen_ms   = get_u64(p + 8);
    st->dst_port        = get_u16(p + 16);
    st->protocol_class  = get_u8(p + 18);
}
