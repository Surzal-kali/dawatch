#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "contracts.h"
#include "ringbuffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>

#define LANE_COUNT 5

typedef struct {
    EngineConfig cfg;
    RingBuffer *lane_buffers[LANE_COUNT];
    double baseline_mean;
    double baseline_var;
    uint64_t baseline_n;
    uint64_t next_baseline_tick_ms;
    uint64_t next_random_tick_ms;
    uint64_t now_ms;
    uint64_t lane_last_counts[LANE_COUNT];
} EngineState;

static uint64_t now_ms(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

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
    int cfd = accept(srv_fd, NULL, NULL);
    if(cfd < 0){
        if(errno != EAGAIN && errno != EWOULDBLOCK)
            perror("accept");
        return;
    }
    // read ContactReport
    ContactReport cr;
    ssize_t n = read(cfd, &cr, sizeof(cr));
    if(n == sizeof(cr)){
        if(cr.lane < LANE_COUNT){
            rb_push(s->lane_buffers[cr.lane], &cr);
            printf("[SOCKET] received contact lane=%d kind=%d ts=%llu\n",
                   cr.lane, cr.kind, (unsigned long long)cr.ts_ms);
        }
    }
    close(cfd);
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
    // Inject canary touches with small probability
    if((rand() % 1000) < 200){ // ~20% per tick
        ContactReport cr = {0};
        cr.ts_ms = s->now_ms;
        cr.lane = 4; // canary
        cr.kind = 1; // read
        // minimal payload
        snprintf((char*)cr.payload, 64, "canary_%u", rand()%100);
        cr.payload_len = 64;
        rb_push(s->lane_buffers[4], &cr);
        printf("[CANARY] touch injected at %llu ms\n", (unsigned long long)s->now_ms);
    }
    // Compute delta canary contacts since last tick
    uint64_t cur_canary = rb_count(s->lane_buffers[4]);
    uint64_t delta_canary = cur_canary - s->lane_last_counts[4];
    s->lane_last_counts[4] = cur_canary;
    // Base simulated activity
    double base = 10.0;
    if((rand() % 1000) < 5) base += 40.0; // 0.5% spike
    base += ((double)rand() / RAND_MAX - 0.5) * 2.0;
    // Weight canary heavily
    double x = base + delta_canary * 100.0;
    return x;
}

static void emit_route(RouteKind kind, const char *reason){
    Route r;
    r.kind = kind;
    r.params = 0;
    r.ttl_ms = 60000;
    printf("[ROUTE] kind=%d ttl=%llu reason=%s\n", r.kind, (unsigned long long)r.ttl_ms, reason);
}

int main(int argc, char **argv){
    srand((unsigned)time(NULL));
    EngineConfig cfg = {
        .baseline_interval_ms = 1000,
        .poisson_lambda_per_min = 2.0,
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
    memset(s.lane_last_counts, 0, sizeof(s.lane_last_counts));
    s.now_ms = now_ms();
    s.next_baseline_tick_ms = s.now_ms + cfg.baseline_interval_ms;
    s.next_random_tick_ms = s.now_ms + (uint64_t)poisson_interval_ms(cfg.poisson_lambda_per_min);

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
            update_baseline(&s, x);
            bool event = baseline_is_event(&s, x);
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
    // usleep(5000);
    {
        struct timespec req = {0, 5 * 1000000};
        nanosleep(&req, NULL);
    }
    }

    for(int i=0;i<LANE_COUNT;i++){
        rb_destroy(s.lane_buffers[i]);
    }
    close(srv_fd);
    unlink(SOCKET_PATH);
    printf("Engine demo finished\n");
    return 0;
}
