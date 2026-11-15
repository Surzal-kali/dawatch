#include "baseline.h"
#include <math.h>
#include <stdlib.h>

struct FeatureWindow {
    double vals[BASELINE_WINDOW_SAMPLES];
    size_t head;      /* next write slot */
    size_t count;     /* live samples, saturates at capacity */
    double sum;       /* rolling sum over live window */
    double sum_sq;    /* rolling sum of squares */
    size_t adds_since_recompute;
};

static size_t oldest_index(const FeatureWindow *fw){
    return (fw->head + BASELINE_WINDOW_SAMPLES - fw->count)
           % BASELINE_WINDOW_SAMPLES;
}

static void recompute(FeatureWindow *fw){
    fw->sum = 0.0;
    fw->sum_sq = 0.0;
    for(size_t i = 0; i < fw->count; i++){
        double v = fw->vals[(oldest_index(fw) + i) % BASELINE_WINDOW_SAMPLES];
        fw->sum += v;
        fw->sum_sq += v * v;
    }
    fw->adds_since_recompute = 0;
}

FeatureWindow *featwin_create(void){
    return calloc(1, sizeof(FeatureWindow));
}

void featwin_destroy(FeatureWindow *fw){
    free(fw);
}

void featwin_push(FeatureWindow *fw, double x){
    if(!fw) return;
    if(fw->count == BASELINE_WINDOW_SAMPLES){
        double old = fw->vals[fw->head];  /* head == oldest when full */
        fw->sum -= old;
        fw->sum_sq -= old * old;
    }else{
        fw->count++;
    }
    fw->vals[fw->head] = x;
    fw->head = (fw->head + 1) % BASELINE_WINDOW_SAMPLES;
    fw->sum += x;
    fw->sum_sq += x * x;
    if(++fw->adds_since_recompute >= BASELINE_WINDOW_SAMPLES)
        recompute(fw);
}

size_t featwin_n(const FeatureWindow *fw){
    return fw ? fw->count : 0;
}

double featwin_mean(const FeatureWindow *fw){
    if(!fw || fw->count == 0) return 0.0;
    return fw->sum / (double)fw->count;
}

double featwin_std(const FeatureWindow *fw){
    if(!fw || fw->count < 2) return 0.0;
    double mean = featwin_mean(fw);
    double var = (fw->sum_sq - mean * mean * (double)fw->count)
                 / ((double)fw->count - 1.0);
    if(var < 0.0) var = 0.0;  /* clamp: rounding can dip below zero on constants */
    return sqrt(var);
}
