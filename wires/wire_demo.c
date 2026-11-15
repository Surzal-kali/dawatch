#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "wire_flow.h"
#include "wire_monitor.h"
#include "wire_source.h"
#include "../engine/platform.h"

static uint64_t now_ms(void){
    return bluec_now_ms();
}

static void msleep(long ms){
    if(ms > 0 && (unsigned long)ms <= 0xFFFFFFFFUL) bluec_sleep_ms((uint32_t)ms);
}

/* Stream sockets may short-write; contacts are exactly sizeof(ContactReport)
 * and the engine's hardened reader expects whole reports. */
static int write_all(bluec_fd_t fd, const void *buf, size_t len){
    const char *p = buf;
    while(len){
        int n = bluec_write(fd, p, len);
        if(n < 0){
            if(bluec_retry()) continue;
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

typedef struct {
    bluec_fd_t fd;
    unsigned sent_tick, sent_beacon, sent_new_peer;
} DemoOut;

static void on_contact(const ContactReport *cr, void *user){
    DemoOut *out = user;
    uint64_t tw = 0, tk = 0, tn = 0;
    switch(cr->kind){
    case WIRE_KIND_FLOW_TICK: {
        WireTickStats st;
        wire_unpack_flow_tick(cr, &st);
        printf("[WIRE] sent FLOW_TICK flows=%u tempo=%.1f+-%.1fms up=%llu down=%llu\n",
               st.flow_count, st.tempo_mean_ms, st.tempo_std_ms,
               (unsigned long long)st.bytes_up, (unsigned long long)st.bytes_down);
        tw = 1;
        break;
    }
    case WIRE_KIND_BEACON_SUSPECT: {
        WireBeaconStats st;
        wire_unpack_beacon(cr, &st);
        printf("[WIRE] sent BEACON_SUSPECT peer=%016llx mean_ms=%u jitter_ppm=%u n=%u\n",
               (unsigned long long)st.peer_token, st.interval_mean_ms,
               st.jitter_ppm, st.occurrences);
        tk = 1;
        break;
    }
    case WIRE_KIND_NEW_PEER: {
        WirePeerStats st;
        wire_unpack_new_peer(cr, &st);
        printf("[WIRE] sent NEW_PEER peer=%016llx port=%u proto=%u\n",
               (unsigned long long)st.peer_token, st.dst_port, st.protocol_class);
        tn = 1;
        break;
    }
    default:
        return;
    }
    if(write_all(out->fd, cr, sizeof(*cr)) == 0){
        out->sent_tick += tw;
        out->sent_beacon += tk;
        out->sent_new_peer += tn;
    }else{
        perror("write");
    }
}

static bluec_fd_t connect_engine(void){
    for(int attempt = 0; attempt < 20; attempt++){
        bluec_fd_t fd = bluec_connect(bluec_socket_path());
        if(fd != BLUEC_FD_INVALID) return fd;
        msleep(300);
    }
    fprintf(stderr, "wire_demo: engine socket not reachable at %s\n", bluec_socket_path());
    return BLUEC_FD_INVALID;
}

int main(int argc, char **argv){
    int duration_s = argc > 1 ? atoi(argv[1]) : 8;
    if(duration_s <= 0) duration_s = 8;

    DemoOut out = { .fd = BLUEC_FD_INVALID };
    out.fd = connect_engine();
    if(out.fd < 0) return 1;

    WireSyntheticConfig scfg = {
        .chatter_flows_per_sec = 30.0,
        .beacon_interval_ms = 700,
        .beacon_port = 8443,
        .salt = 0xC0FFEEULL,     // demo: ids are already fiction
        .seed = 42,              // fixed seed: reproducible demo runs
        .anchor_ms = now_ms(),
    };
    WireSource *src = wire_synthetic_source_create(&scfg);
    WireMonitor *mon = wire_monitor_create(WIRE_FLOW_CAPACITY_DEFAULT,
                                           WIRE_PEER_CAPACITY_DEFAULT,
                                           on_contact, &out);
    if(!src || !mon){ fprintf(stderr, "wire_demo: init failed\n"); return 1; }

    printf("[WIRE] demo: %ds, chatter 30 flows/s + beaconer @700ms on port 8443\n",
           duration_s);

    uint64_t start = now_ms();
    uint64_t deadline = start + (uint64_t)duration_s * 1000ULL;
    uint64_t next_window = start + 1000;
    uint64_t now;
    while((now = now_ms()) < deadline){
        WireFlow f;
        while(wire_source_next(src, &f, now))
            wire_monitor_push(mon, &f);
        if(now >= next_window){
            wire_monitor_window_tick(mon, now);
            next_window += 1000;
        }
        msleep(20);
    }
    wire_monitor_window_tick(mon, now);  // close the final partial window

    printf("[WIRE] demo summary: flows_in_window=%zu peers_tracked=%zu "
           "contacts: tick=%u beacon=%u new_peer=%u\n",
           wire_monitor_flow_count(mon), wire_monitor_peer_count(mon),
           out.sent_tick, out.sent_beacon, out.sent_new_peer);

    wire_source_destroy(src);
    wire_monitor_destroy(mon);
    bluec_close(out.fd);
    return 0;
}
