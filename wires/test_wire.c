#include "wire_flow.h"
#include "wire_monitor.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Wire lane conformance (main README §11 + wires.README):
 * aggregation math, cadence detection, directionality, memory bounds, ring
 * eviction — and the raw-content prohibitions enforced as negative tests:
 * packers never write past payload_len, no raw identifier/credential bytes
 * ever appear in a contact payload. */

#define MAX_CONTACTS 256

typedef struct {
    ContactReport seen[MAX_CONTACTS];
    size_t n;
} ContactLog;

static void log_cb(const ContactReport *cr, void *user){
    ContactLog *log = user;
    if(log->n < MAX_CONTACTS) log->seen[log->n++] = *cr;
}

static size_t count_kind(const ContactLog *log, uint8_t kind){
    size_t c = 0;
    for(size_t i = 0; i < log->n; i++)
        if(log->seen[i].kind == kind && log->seen[i].lane == LANE_WIRE) c++;
    return c;
}

static const ContactReport *last_kind(const ContactLog *log, uint8_t kind){
    for(size_t i = log->n; i > 0; i--)
        if(log->seen[i-1].kind == kind && log->seen[i-1].lane == LANE_WIRE)
            return &log->seen[i-1];
    return NULL;
}

static WireFlow mk_flow(uint64_t token, uint64_t start, uint64_t up, uint64_t down){
    WireFlow f = {0};
    f.peer_token = token;
    f.flow_start_ms = start;
    f.flow_last_ms = start + 10;
    f.pkt_count = 2;
    f.bytes_up = up;
    f.bytes_down = down;
    f.dst_port = 443;
    f.protocol_class = WIRE_PROTO_TCP;
    return f;
}

/* ---- tokenization ---- */
static void test_tokenization(void){
    uint64_t t1 = wire_peer_token("10.0.0.1", 7);
    assert(t1 != 0);
    assert(wire_peer_token("10.0.0.1", 7) == t1);          // consistent
    assert(wire_peer_token("10.0.0.2", 7) != t1);          // distinct ids
    assert(wire_peer_token("10.0.0.1", 8) != t1);          // salt keys the map
    assert(wire_peer_token(NULL, 7) == 0);                 // 0 = untracked
    assert(wire_peer_token("", 7) != 0);
}

/* ---- payload discipline: truthful payload_len, never past the bound ---- */
static void test_payload_bound(void){
    const uint8_t SENTINEL = 0xA5;
    ContactReport r;
    memset(r.payload, SENTINEL, sizeof(r.payload));

    WireTickStats tick = { .tempo_mean_ms = 100.5, .tempo_std_ms = 12.25,
                           .flow_count = 9, .bytes_up = 111, .bytes_down = 222 };
    wire_pack_flow_tick(&r, 1, &tick);
    assert(r.lane == LANE_WIRE && r.kind == WIRE_KIND_FLOW_TICK);
    assert(r.payload_len == 40 && r.payload_len <= 128);
    for(size_t i = r.payload_len; i < sizeof(r.payload); i++)
        assert(r.payload[i] == SENTINEL);                  // bound respected
    WireTickStats back;
    wire_unpack_flow_tick(&r, &back);
    assert(back.tempo_mean_ms == 100.5 && back.tempo_std_ms == 12.25);
    assert(back.flow_count == 9 && back.bytes_up == 111 && back.bytes_down == 222);

    memset(r.payload, SENTINEL, sizeof(r.payload));
    WireBeaconStats bc = { .peer_token = 0xdeadbeefcafeULL, .first_seen_ms = 5,
                           .last_seen_ms = 5005, .interval_mean_ms = 1000,
                           .jitter_ppm = 1234, .occurrences = 7 };
    wire_pack_beacon(&r, 2, &bc);
    assert(r.payload_len == 36 && r.payload_len <= 128);
    for(size_t i = r.payload_len; i < sizeof(r.payload); i++)
        assert(r.payload[i] == SENTINEL);
    WireBeaconStats bb;
    wire_unpack_beacon(&r, &bb);
    assert(bb.peer_token == 0xdeadbeefcafeULL && bb.interval_mean_ms == 1000);
    assert(bb.jitter_ppm == 1234 && bb.occurrences == 7);
    assert(bb.first_seen_ms == 5 && bb.last_seen_ms == 5005);

    memset(r.payload, SENTINEL, sizeof(r.payload));
    WirePeerStats np = { .peer_token = 42, .first_seen_ms = 9,
                         .dst_port = 8443, .protocol_class = WIRE_PROTO_TCP };
    wire_pack_new_peer(&r, 3, &np);
    assert(r.payload_len == 20 && r.payload_len <= 128);
    for(size_t i = r.payload_len; i < sizeof(r.payload); i++)
        assert(r.payload[i] == SENTINEL);
    WirePeerStats nn;
    wire_unpack_new_peer(&r, &nn);
    assert(nn.peer_token == 42 && nn.dst_port == 8443);
    assert(nn.protocol_class == WIRE_PROTO_TCP && nn.first_seen_ms == 9);
}

/* ---- raw-content prohibitions, as negative tests ---- */
static void test_no_raw_content_in_payloads(void){
    static const char *must_not_leak[] = {
        "192.168", "secret", "hunter2", "creds", "BEGIN RSA",
    };
    // "real" ids and a credential-looking string tokenized through the lane
    uint64_t t = wire_peer_token("secret-user@192.168.1.7", 0xC0FFEE);
    uint64_t t2 = wire_peer_token("hunter2creds", 0xC0FFEE);

    ContactReport r;
    WireBeaconStats bc = { .peer_token = t, .first_seen_ms = 1,
                           .last_seen_ms = 601, .interval_mean_ms = 100,
                           .jitter_ppm = 0, .occurrences = 6 };
    wire_pack_beacon(&r, 10, &bc);
    assert(r.payload_len == 36);
    for(size_t k = 0; k < sizeof(must_not_leak)/sizeof(*must_not_leak); k++){
        size_t mlen = strlen(must_not_leak[k]);
        for(size_t i = 0; i + mlen <= r.payload_len; i++)
            assert(memcmp(&r.payload[i], must_not_leak[k], mlen) != 0);
    }
    WirePeerStats np = { .peer_token = t2, .first_seen_ms = 2,
                         .dst_port = 443, .protocol_class = WIRE_PROTO_TCP };
    wire_pack_new_peer(&r, 11, &np);
    assert(r.payload_len == 20);
    for(size_t k = 0; k < sizeof(must_not_leak)/sizeof(*must_not_leak); k++){
        size_t mlen = strlen(must_not_leak[k]);
        for(size_t i = 0; i + mlen <= r.payload_len; i++)
            assert(memcmp(&r.payload[i], must_not_leak[k], mlen) != 0);
    }
}

/* ---- aggregation: window math + directionality + reset ---- */
static void test_window_aggregation(void){
    ContactLog log = {0};
    WireMonitor *m = wire_monitor_create(64, 32, log_cb, &log);
    assert(m);
    uint64_t tok = wire_peer_token("agg-peer", 1);
    wire_monitor_push(m, &(WireFlow){0});               // token 0: untracked
    wire_monitor_push(m, &(WireFlow){ .peer_token = tok, .flow_start_ms = 0,
                        .flow_last_ms = 10, .bytes_up = 100, .bytes_down = 900 });
    wire_monitor_push(m, &(WireFlow){ .peer_token = tok, .flow_start_ms = 100,
                        .flow_last_ms = 110, .bytes_up = 10, .bytes_down = 1 });
    wire_monitor_push(m, &(WireFlow){ .peer_token = tok, .flow_start_ms = 200,
                        .flow_last_ms = 210, .bytes_up = 400, .bytes_down = 5 });
    wire_monitor_window_tick(m, 250);
    assert(log.n == 2);                                  // NEW_PEER + FLOW_TICK
    const ContactReport *tick = last_kind(&log, WIRE_KIND_FLOW_TICK);
    assert(tick);
    WireTickStats st;
    wire_unpack_flow_tick(tick, &st);
    // token-0 traffic still counts in window volume (4 flows) but never in
    // peer memory — that's the untracked semantics
    assert(st.flow_count == 4);
    assert(st.bytes_up == 510 && st.bytes_down == 906);  // direction split true
    assert(fabs(st.tempo_mean_ms - 100.0) < 1e-9);       // deltas 100,100
    assert(fabs(st.tempo_std_ms - 0.0) < 1e-6);

    log.n = 0;
    wire_monitor_push(m, &(WireFlow){ .peer_token = tok, .flow_start_ms = 300,
                        .flow_last_ms = 310, .bytes_up = 1, .bytes_down = 2 });
    assert(wire_monitor_window_tick(m, 350));
    tick = last_kind(&log, WIRE_KIND_FLOW_TICK);
    wire_unpack_flow_tick(tick, &st);
    assert(st.flow_count == 1);                          // window was reset
    assert(st.tempo_mean_ms == 0.0 && st.tempo_std_ms == 0.0); // no deltas
    assert(!wire_monitor_window_tick(m, 400));           // empty window: silent
    wire_monitor_destroy(m);
}

/* ---- cadence: beacon fires on regularity, suppressed per episode ---- */
static void test_cadence_beacon(void){
    ContactLog log = {0};
    WireMonitor *m = wire_monitor_create(64, 32, log_cb, &log);
    assert(m);
    uint64_t tok = wire_peer_token("beaconer", 2);
    uint64_t t = 0;
    // 9 arrivals, 8 exact 1000ms intervals -> fires once (6th interval), stays quiet
    for(int i = 0; i < 9; i++, t += 1000){
        WireFlow f = mk_flow(tok, t, 64, 64);
        wire_monitor_push(m, &f);
    }
    assert(count_kind(&log, WIRE_KIND_BEACON_SUSPECT) == 1);
    const ContactReport *b = last_kind(&log, WIRE_KIND_BEACON_SUSPECT);
    WireBeaconStats st;
    wire_unpack_beacon(b, &st);
    assert(st.interval_mean_ms == 1000 && st.jitter_ppm == 0);
    assert(st.occurrences == 6);
    assert(st.peer_token == tok);

    // irregular cadence: re-arms (no spam), then a fresh regular series fires again
    t += 4000; wire_monitor_push(m, &(WireFlow){0});     // keep struct init clean
    log.n = 0;  // only counting beacons from here; pushes below re-log
    WireFlow f = mk_flow(tok, 12000, 64, 64);            // interval 4000
    wire_monitor_push(m, &f);
    f = mk_flow(tok, 24000, 64, 64);                     // interval 12000
    wire_monitor_push(m, &f);
    t = 25000;
    for(int i = 0; i < 8; i++, t += 1000){               // 8 fresh exact intervals
        f = mk_flow(tok, t, 64, 64);
        wire_monitor_push(m, &f);
    }
    assert(count_kind(&log, WIRE_KIND_BEACON_SUSPECT) == 1);  // second episode
    wire_monitor_destroy(m);
}

/* ---- cadence: jittered traffic never beacons ---- */
static void test_cadence_jitter_stays_quiet(void){
    ContactLog log = {0};
    WireMonitor *m = wire_monitor_create(64, 32, log_cb, &log);
    assert(m);
    uint64_t tok = wire_peer_token("chatty", 3);
    static const uint64_t iv[] = { 400, 1600, 700, 1300, 850, 1150, 600, 1400, 950, 1050 };
    uint64_t t = 0;
    for(size_t i = 0; i < sizeof(iv)/sizeof(*iv); i++){
        t += iv[i];
        wire_monitor_push(m, &(WireFlow){ .peer_token = tok, .flow_start_ms = t,
                            .flow_last_ms = t + 5, .pkt_count = 4,
                            .bytes_up = 6000, .bytes_down = 20000,
                            .dst_port = 443, .protocol_class = WIRE_PROTO_TCP });
    }
    assert(count_kind(&log, WIRE_KIND_BEACON_SUSPECT) == 0);
    wire_monitor_destroy(m);
}

/* ---- memory bounds: O(window), capacity stays capacity ---- */
static void test_memory_bounds(void){
    ContactLog log = {0};
    WireMonitor *m = wire_monitor_create(16, 8, log_cb, &log);
    assert(m);
    for(int i = 0; i < 160; i++){
        uint64_t tok = wire_peer_token("peer", (uint64_t)(i % 20 + 1));  // 20 ids > 8 slots
        wire_monitor_push(m, &(WireFlow){ .peer_token = tok, .flow_start_ms = (uint64_t)i,
                            .flow_last_ms = (uint64_t)i + 1,
                            .bytes_up = 10, .bytes_down = 20 });
    }
    assert(wire_monitor_flow_count(m) == 16);            // ring evicts, never grows
    assert(wire_monitor_peer_count(m) == 8);             // table capacity stays capacity
    assert(count_kind(&log, WIRE_KIND_BEACON_SUSPECT) == 0);  // 1ms chatter: no cadence
    wire_monitor_destroy(m);
}

/* ---- out-of-order / adversarial timestamps: skip, never corrupt ---- */
static void test_out_of_order_safe(void){
    ContactLog log = {0};
    WireMonitor *m = wire_monitor_create(16, 8, log_cb, &log);
    assert(m);
    uint64_t tok = wire_peer_token("ooo", 4);
    wire_monitor_push(m, &(WireFlow){ .peer_token = tok, .flow_start_ms = 5000,
                        .flow_last_ms = 5001 });
    wire_monitor_push(m, &(WireFlow){ .peer_token = tok, .flow_start_ms = 1000,
                        .flow_last_ms = 1001 });         // backwards: skipped
    wire_monitor_push(m, &(WireFlow){ .peer_token = tok, .flow_start_ms = 2000,
                        .flow_last_ms = 2001 });
    assert(count_kind(&log, WIRE_KIND_BEACON_SUSPECT) == 0);
    assert(wire_monitor_peer_count(m) == 1);
    wire_monitor_push(m, NULL);                          // defensive no-crash
    wire_monitor_destroy(m);
}

int main(void){
    test_tokenization();
    test_payload_bound();
    test_no_raw_content_in_payloads();
    test_window_aggregation();
    test_cadence_beacon();
    test_cadence_jitter_stays_quiet();
    test_memory_bounds();
    test_out_of_order_safe();
    printf("test_wire: PASS\n");
    return 0;
}
