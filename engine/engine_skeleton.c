#include "contracts.h"
#include "ringbuffer.h"
#include <time.h>
#include <stdlib.h>
#include <math.h>

static double poisson_interval_ms(double lambda_per_min) {
    // return inter-arrival time in ms using -ln(U)/lambda
    double u = (double)rand() / RAND_MAX + 1e-12;
    double lambda_per_ms = lambda_per_min / (60.0 * 1000.0);
    return -log(u) / lambda_per_ms;
}

/* Cheap baseline: every baseline_interval_ms we compute a lightweight
   WindowFeatures from per-lane aggregated counters.
   Deviation detection is K-sigma vs rolling mean/std of baseline features.
*/
typedef struct {
    EngineConfig cfg;
    RingBuffer *lane_buffers[5];
    // rolling stats for baseline tempo
    double baseline_mean;
    double baseline_var;
    uint64_t baseline_n;
} EngineState;

EngineState* engine_init(EngineConfig cfg) {
    EngineState *s = calloc(1, sizeof(EngineState));
    s->cfg = cfg;
    for(int i=0;i<5;i++){
        s->lane_buffers[i] = rb_create(cfg.ringbuf_capacity, sizeof(ContactReport));
    }
    s->baseline_mean = 0;
    s->baseline_var = 0;
    s->baseline_n = 0;
    srand((unsigned)time(NULL));
    return s;
}

/* Update rolling mean/var incrementally */
static void update_baseline(EngineState *s, double x){
    s->baseline_n++;
    double delta = x - s->baseline_mean;
    s->baseline_mean += delta / s->baseline_n;
    double delta2 = x - s->baseline_mean;
    s->baseline_var += delta * delta2;
}

/* Decide if current window is event */
bool baseline_is_event(EngineState *s, double x){
    if(s->baseline_n < 30) return false;
    double std = sqrt(s->baseline_var / (s->baseline_n -1));
    if(std < 1e-9) return false;
    double z = fabs(x - s->baseline_mean)/std;
    return z > s->cfg.k_sigma_event;
}
