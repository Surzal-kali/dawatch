#include "wire_monitor.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t token;
    uint64_t first_seen_ms;
    uint64_t last_start_ms;      // previous flow_start_ms for interval math
    uint64_t intervals[WIRE_CADENCE_K];  // ring of recent inter-arrivals
    size_t   interval_head;
    size_t   interval_count;
    bool     sighted;            // NEW_PEER already fired for this entry
    bool     beacon_active;      // regularity episode already reported
    bool     used;
} WirePeer;

struct WireMonitor {
    RingBuffer *flows;           // O(window): WireFlow values
    WirePeer *peers;             // fixed table, round-robin slot reuse
    size_t peer_cap;
    size_t peer_next;            // next slot to (re)use when full
    size_t peer_live;
    wire_contact_fn cb;
    void *user;
    /* window accumulators (reset each tick) */
    uint64_t win_flows;
    uint64_t win_bytes_up;
    uint64_t win_bytes_down;
    double tempo_sum;            // inter-flow-arrival deltas this window
    double tempo_sum2;
    uint64_t tempo_count;
    uint64_t last_flow_ms;
};

WireMonitor *wire_monitor_create(size_t flow_capacity, size_t peer_capacity,
                                 wire_contact_fn cb, void *user){
    if(flow_capacity == 0 || peer_capacity == 0) return NULL;
    WireMonitor *m = calloc(1, sizeof(*m));
    if(!m) return NULL;
    m->flows = rb_create(flow_capacity, sizeof(WireFlow));
    if(!m->flows){ free(m); return NULL; }
    m->peers = calloc(peer_capacity, sizeof(WirePeer));
    if(!m->peers){ rb_destroy(m->flows); free(m); return NULL; }
    m->peer_cap = peer_capacity;
    m->cb = cb;
    m->user = user;
    return m;
}

void wire_monitor_destroy(WireMonitor *m){
    if(!m) return;
    rb_destroy(m->flows);
    free(m->peers);
    free(m);
}

static WirePeer *peer_slot(WireMonitor *m, uint64_t token){
    WirePeer *free_slot = NULL;
    for(size_t i = 0; i < m->peer_cap; i++){
        WirePeer *p = &m->peers[i];
        if(p->used && p->token == token) return p;
        if(!p->used && !free_slot) free_slot = p;
    }
    if(!free_slot){
        free_slot = &m->peers[m->peer_next];
        m->peer_next = (m->peer_next + 1) % m->peer_cap;
        if(free_slot->used) m->peer_live--;
    }
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->token = token;
    free_slot->used = true;
    m->peer_live++;
    return free_slot;
}

static void emit(WireMonitor *m, const ContactReport *cr){
    if(m->cb) m->cb(cr, m->user);
}

static double intervals_mean(const uint64_t *iv, size_t n){
    double s = 0;
    for(size_t i = 0; i < n; i++) s += (double)iv[i];
    return n ? s / (double)n : 0.0;
}

static double intervals_std(const uint64_t *iv, size_t n, double mean){
    if(n < 2) return 0.0;
    double s2 = 0;
    for(size_t i = 0; i < n; i++){
        double d = (double)iv[i] - mean;
        s2 += d * d;
    }
    return sqrt(s2 / (double)n);
}

static void check_beacon(WireMonitor *m, WirePeer *p, uint64_t now){
    size_t n = p->interval_count < WIRE_CADENCE_K ? p->interval_count : WIRE_CADENCE_K;
    if(n < WIRE_BEACON_MIN_INTERVALS) return;
    uint64_t window[WIRE_CADENCE_K];
    for(size_t i = 0; i < n; i++)
        window[i] = p->intervals[(p->interval_head + WIRE_CADENCE_K - 1 - i) % WIRE_CADENCE_K];
    double mean = intervals_mean(window, n);
    double std = intervals_std(window, n, mean);
    uint32_t jitter_ppm = mean > 0.0
        ? (uint32_t)(std / mean * 1e6)
        : UINT32_MAX;
    bool regular = mean >= (double)WIRE_BEACON_MEAN_MS_MIN
                && mean <= (double)WIRE_BEACON_MEAN_MS_MAX
                && jitter_ppm <= WIRE_BEACON_JITTER_PPM;
    if(regular && !p->beacon_active){
        p->beacon_active = true;
        WireBeaconStats st = {
            .peer_token = p->token,
            .first_seen_ms = p->first_seen_ms,
            .last_seen_ms = now,
            .interval_mean_ms = (uint32_t)(mean + 0.5),
            .jitter_ppm = jitter_ppm,
            .occurrences = (uint16_t)n,
        };
        ContactReport cr;
        wire_pack_beacon(&cr, now, &st);
        emit(m, &cr);
    }else if(!regular){
        p->beacon_active = false;  // irregularity re-arms the episode
    }
}

void wire_monitor_push(WireMonitor *m, const WireFlow *f){
    if(!m || !f) return;
    uint64_t now = f->flow_start_ms;

    // window accumulators
    m->win_flows++;
    m->win_bytes_up += f->bytes_up;
    m->win_bytes_down += f->bytes_down;
    if(m->last_flow_ms && now > m->last_flow_ms){
        double d = (double)(now - m->last_flow_ms);
        m->tempo_sum += d;
        m->tempo_sum2 += d * d;
        m->tempo_count++;
    }
    if(now >= m->last_flow_ms) m->last_flow_ms = now;

    // peer cadence + new-peer membership (token 0 = untracked)
    if(f->peer_token){
        WirePeer *p = peer_slot(m, f->peer_token);
        if(!p->first_seen_ms) p->first_seen_ms = now;
        if(p->last_start_ms && now > p->last_start_ms){
            uint64_t interval = now - p->last_start_ms;
            p->intervals[p->interval_head] = interval;
            p->interval_head = (p->interval_head + 1) % WIRE_CADENCE_K;
            if(p->interval_count < WIRE_CADENCE_K) p->interval_count++;
        }
        p->last_start_ms = now;
        if(!p->sighted){  // first sighting within peer-table memory
            p->sighted = true;
            WirePeerStats st = {
                .peer_token = f->peer_token,
                .first_seen_ms = now,
                .dst_port = f->dst_port,
                .protocol_class = f->protocol_class,
            };
            ContactReport cr;
            wire_pack_new_peer(&cr, now, &st);
            emit(m, &cr);
        }
        check_beacon(m, p, now);
    }

    // retained high-res window (features only — never raw packets)
    rb_push(m->flows, f);
}

bool wire_monitor_window_tick(WireMonitor *m, uint64_t now){
    if(!m || m->win_flows == 0) return false;
    WireTickStats st = {0};
    st.flow_count = (uint32_t)m->win_flows;
    st.bytes_up = m->win_bytes_up;
    st.bytes_down = m->win_bytes_down;
    if(m->tempo_count){
        double mean = m->tempo_sum / (double)m->tempo_count;
        double var = m->tempo_sum2 / (double)m->tempo_count - mean * mean;
        st.tempo_mean_ms = mean;
        st.tempo_std_ms = var > 0.0 ? sqrt(var) : 0.0;
    }
    ContactReport cr;
    wire_pack_flow_tick(&cr, now, &st);
    emit(m, &cr);

    m->win_flows = 0;
    m->win_bytes_up = 0;
    m->win_bytes_down = 0;
    m->tempo_sum = 0;
    m->tempo_sum2 = 0;
    m->tempo_count = 0;
    m->last_flow_ms = 0;  // tempo stays per-window: skip cross-window deltas
    return true;
}

size_t wire_monitor_flow_count(const WireMonitor *m){
    return m ? rb_count(m->flows) : 0;
}

size_t wire_monitor_peer_count(const WireMonitor *m){
    return m ? m->peer_live : 0;
}
