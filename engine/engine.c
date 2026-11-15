#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "contracts.h"
#include "ingest.h"
#include "ringbuffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include <math.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>

typedef enum {
    CONTACT_CHECKIN = 1,       // engine-generated jitter/activity
    CONTACT_HONEYPOT_TOUCH = 2,// real external interaction
    CONTACT_CANARY_PROBE = 3   // scheduled inspection/probe
} ContactKind;

/* Demo/SMOKE fiction: rate of the genuine-deviation spike scenario. Poisson,
 * slightly above the random-burst lambda so a 30s run essentially always has
 * a spike after the 30-sample warm-up for the event trigger to catch. */
#define DEMO_SPIKE_LAMBDA_PER_MIN 20.0

typedef struct {
    EngineConfig cfg;
    RingBuffer *lane_buffers[LANE_COUNT];
    IngestCounters ing;
    double baseline_mean;
    double baseline_var;
    uint64_t baseline_n;
    uint64_t next_baseline_tick_ms;
    uint64_t next_random_tick_ms;
    uint64_t next_spike_tick_ms;
    uint64_t now_ms;
} EngineState;

static uint64_t now_ms(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

static void emit_route(RouteKind kind, const char *reason);

#define SOCKET_PATH "/tmp/bluec.socket"

static int socket_init(void){
    unlink(SOCKET_PATH);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd < 0) return -1;
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path)-1);
    if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){ close(fd); return -1; }
    if(listen(fd, 8) < 0){ close(fd); return -1; }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

static void socket_accept_and_push(int srv_fd, EngineState *s){
    // hardening: accept ALL pending connections per loop pass, not just one
    for(;;){
        int cfd = accept(srv_fd, NULL, NULL);
        if(cfd < 0){
            if(errno != EAGAIN && errno != EWOULDBLOCK)
                perror("accept");
            return;
        }
        // drain every contact the connection carries before closing
        ContactReport cr;
        for(;;){
            int rc = ingest_read_contact(cfd, &cr);
            if(rc <= 0) break;
            if(!ingest_validate(&cr)){
                s->ing.malformed++;
                printf("[INGEST] malformed contact rejected lane=%u payload_len=%u\n",
                       cr.lane, cr.payload_len);
                continue;
            }
            RouteKind route = ingest_contact(s->lane_buffers, &s->ing, &cr, true);
            printf("[SOCKET] received contact lane=%d kind=%d ts=%llu\n",
                   cr.lane, cr.kind, (unsigned long long)cr.ts_ms);
            if(route == ROUTE_ALERT)
                emit_route(ROUTE_ALERT, "canary touch");
        }
        close(cfd);
    }
}

static double poisson_interval_ms(double lambda_per_min){
    double u = (double)rand() / (RAND_MAX + 1.0);
    if(u <= 0) u = 1e-12;
    double lambda_per_ms = lambda_per_min / (60.0 * 1000.0);
    return -log(u) / lambda_per_ms;
}

static void update_baseline(EngineState *s, double x){
    s->baseline_n++;
    double delta = x - s->baseline_mean;
    s->baseline_mean += delta / s->baseline_n;
    double delta2 = x - s->baseline_mean;
    s->baseline_var += delta * delta2;
}

static bool baseline_is_event(EngineState *s, double x){
    if(s->baseline_n < 30) return false;
    double std = sqrt(s->baseline_var / (s->baseline_n - 1));
    if(std < 1e-9) return false;
    double z = fabs(x - s->baseline_mean) / std;
    return z > s->cfg.k_sigma_event;
}

static double simulate_window_feature(EngineState *s){
    // Synthetic canary liveness: engine-generated, so no ALERT and no say in
    // the fused scalar (canary split — engine.README matrix). Still stored and
    // counted on its lane so the buffer/counter paths stay exercised.
    if((rand() % 1000) < 200){ // ~20% per tick
        ContactReport cr = {0};
        cr.ts_ms = s->now_ms;
        cr.lane = LANE_CANARY;
        cr.kind = CONTACT_CHECKIN;
        // minimal payload
        int written = snprintf(
            (char *)cr.payload,
            sizeof(cr.payload),
            "canary_%u",
            rand() % 100
        );

        if (written < 0) {
            cr.payload_len = 0;
        } else if ((size_t)written >= sizeof(cr.payload)) {
            cr.payload_len = sizeof(cr.payload);
        } else {
            cr.payload_len = (uint16_t)written;
        }
        ingest_contact(s->lane_buffers, &s->ing, &cr, false);
        printf("[CHECKIN] synthetic touch injected ...\n");
    }
    // Genuine deviation scenario for the demo/SMOKE: spike arrivals are
    // Poisson-drawn (own demo rate, never a fixed tempo). These are the
    // windows event triggers are meant to catch.
    double base = 10.0;
    if(s->now_ms >= s->next_spike_tick_ms){
        s->next_spike_tick_ms = s->now_ms +
            (uint64_t)poisson_interval_ms(DEMO_SPIKE_LAMBDA_PER_MIN);
        base += 40.0;
    }
    base += ((double)rand() / RAND_MAX - 0.5) * 2.0;
    return base;
}

static void emit_route(RouteKind kind, const char *reason){
    Route r;
    r.kind = kind;
    r.params = 0;
    r.ttl_ms = 60000;
    printf("[ROUTE] kind=%d ttl=%llu reason=%s\n", r.kind, (unsigned long long)r.ttl_ms, reason);
}

int main(void){
    srand((unsigned)time(NULL));
    EngineConfig cfg = {
        .baseline_interval_ms = 200,
        // demo config must demonstrably fire every fireable trigger within the
        // 30s run window (main.README §11): a 12/min rate gives ~6 random
        // bursts per run; at the old 2/min a run had ~37% chance of zero.
        .poisson_lambda_per_min = 12.0,
        .k_sigma_event = 3.0,
        .ringbuf_capacity = 1024
    };

    EngineState s = {0};
    s.cfg = cfg;
    for(int i=0;i<LANE_COUNT;i++){
        s.lane_buffers[i] = rb_create(cfg.ringbuf_capacity, sizeof(ContactReport));
    }
    s.baseline_mean = 0;
    s.baseline_var = 0;
    s.baseline_n = 0;
    s.now_ms = now_ms();
    s.next_baseline_tick_ms = s.now_ms + cfg.baseline_interval_ms;
    s.next_random_tick_ms = s.now_ms + (uint64_t)poisson_interval_ms(cfg.poisson_lambda_per_min);
    s.next_spike_tick_ms = s.now_ms + (uint64_t)poisson_interval_ms(DEMO_SPIKE_LAMBDA_PER_MIN);

    printf("Engine starting: baseline_interval=%llu ms, lambda=%.2f/min, k=%.1f\n",
           (unsigned long long)cfg.baseline_interval_ms,
           cfg.poisson_lambda_per_min,
           cfg.k_sigma_event);

    int srv_fd = socket_init();
    if(srv_fd < 0){ perror("socket_init"); return 1; }
    printf("[SOCKET] listening on %s\n", SOCKET_PATH);

    uint64_t loop_end = s.now_ms + 30000; // run 30s demo
    while(s.now_ms < loop_end){
        s.now_ms = now_ms();
        socket_accept_and_push(srv_fd, &s);
        // baseline tick
        if(s.now_ms >= s.next_baseline_tick_ms){
            s.next_baseline_tick_ms += cfg.baseline_interval_ms;
            double x = simulate_window_feature(&s);
            bool event = baseline_is_event(&s, x);
            update_baseline(&s, x);
            printf("[BASELINE] n=%llu mean=%.2f var=%.2f x=%.2f event=%d\n",
                   (unsigned long long)s.baseline_n, s.baseline_mean, s.baseline_var, x, event);
            if(event){
                emit_route(ROUTE_BURST, "event deviation");
            }
        }
        // random trigger
        if(s.now_ms >= s.next_random_tick_ms){
            s.next_random_tick_ms = s.now_ms + (uint64_t)poisson_interval_ms(cfg.poisson_lambda_per_min);
            printf("[RANDOM] Poisson trigger at %llu ms\n", (unsigned long long)s.now_ms);
            emit_route(ROUTE_BURST, "random unknown-unknown");
        }
    }
    // usleep(5000);
    {
        struct timespec req = {0, 5 * 1000000};

    
        nanosleep(&req, NULL);
    }
    printf("[CHECKIN] Demo completed successfully\n");
    printf("[INGEST] lifetime ingress: wire=%llu endpoint=%llu auth=%llu api=%llu canary=%llu dropped=%llu malformed=%llu canary_alerts=%llu\n",
           (unsigned long long)s.ing.ingress[LANE_WIRE],
           (unsigned long long)s.ing.ingress[LANE_ENDPOINT],
           (unsigned long long)s.ing.ingress[LANE_AUTH],
           (unsigned long long)s.ing.ingress[LANE_API],
           (unsigned long long)s.ing.ingress[LANE_CANARY],
           (unsigned long long)s.ing.dropped,
           (unsigned long long)s.ing.malformed,
           (unsigned long long)s.ing.canary_alerts);

    for(int i=0;i<LANE_COUNT;i++){
        rb_destroy(s.lane_buffers[i]);
    }
    close(srv_fd);
    unlink(SOCKET_PATH);
    printf("Engine demo finished\n");
    return 0;
}
