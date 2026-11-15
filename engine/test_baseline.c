#include "baseline.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

/* Windowed baseline conformance (engine.README matrix):
 * exact window stats, eviction truth, drift bound, n saturation. Plain
 * asserts, gcc+make only. */

static void close_to(double got, double want, double eps, const char *what){
    if(fabs(got - want) > eps){
        fprintf(stderr, "FAIL: %s got %.12f want %.12f\n", what, got, want);
        assert(0 && what);
    }
}

static void test_empty(void){
    FeatureWindow *fw = featwin_create();
    assert(fw);
    assert(featwin_n(fw) == 0);
    assert(featwin_mean(fw) == 0.0);
    assert(featwin_std(fw) == 0.0);
    featwin_push(fw, 7.0);
    assert(featwin_n(fw) == 1);
    assert(featwin_mean(fw) == 7.0);
    assert(featwin_std(fw) == 0.0);          // std undefined below n=2
    featwin_destroy(fw);
}

static void test_exact_stats(void){
    FeatureWindow *fw = featwin_create();
    for(int i = 1; i <= 100; i++) featwin_push(fw, (double)i);
    assert(featwin_n(fw) == 100);
    close_to(featwin_mean(fw), 50.5, 1e-9, "mean 1..100");
    // sample variance of 1..n is n(n+1)/12 -> std = sqrt(100*101/12)
    close_to(featwin_std(fw), sqrt(100.0 * 101.0 / 12.0), 1e-7, "std 1..100");
    featwin_destroy(fw);
}

static void test_constant_std_zero(void){
    FeatureWindow *fw = featwin_create();
    for(int i = 0; i < 64; i++) featwin_push(fw, 10.0);
    close_to(featwin_mean(fw), 10.0, 1e-12, "constant mean");
    assert(featwin_std(fw) == 0.0);          // clamped, not NaN-negative
    featwin_destroy(fw);
}

static void test_eviction(void){
    FeatureWindow *fw = featwin_create();
    for(int i = 0; i < 512; i++) featwin_push(fw, 0.0);   // fill with zeros
    assert(featwin_n(fw) == BASELINE_WINDOW_SAMPLES);
    assert(featwin_mean(fw) == 0.0);
    for(int i = 0; i < 512; i++) featwin_push(fw, 50.0);  // evict every zero
    assert(featwin_n(fw) == BASELINE_WINDOW_SAMPLES);     // bounded, never grows
    close_to(featwin_mean(fw), 50.0, 1e-9, "eviction mean");
    assert(featwin_std(fw) == 0.0);
    featwin_destroy(fw);
}

/* Evicted samples must leave the stats: push a step-change and confirm the
 * baseline fully absorbs the new regime once the old regime has aged out. */
static void test_step_change_rearms(void){
    FeatureWindow *fw = featwin_create();
    for(int i = 0; i < 512; i++) featwin_push(fw, 10.0);
    for(int i = 0; i < 200; i++) featwin_push(fw, 60.0);  // regime shift
    // old regime is 312/512 of the window: mean pulled toward 10 but honest
    close_to(featwin_mean(fw), (312.0 * 10.0 + 200.0 * 60.0) / 512.0, 1e-7,
             "blended mean during shift");
    for(int i = 0; i < 312; i++) featwin_push(fw, 60.0);  // age out old regime
    close_to(featwin_mean(fw), 60.0, 1e-9, "mean after full age-out");
    assert(featwin_std(fw) == 0.0);
    featwin_destroy(fw);
}

/* Push past one full capacity cycle: wrap arithmetic, oldest-out exactness. */
static void test_wrap_exactness(void){
    FeatureWindow *fw = featwin_create();
    // values 1..600; live window must be 89..600 (arithmetic run of 512)
    for(int i = 1; i <= 600; i++) featwin_push(fw, (double)i);
    assert(featwin_n(fw) == BASELINE_WINDOW_SAMPLES);
    close_to(featwin_mean(fw), (89.0 + 600.0) / 2.0, 1e-9, "wrap mean");
    close_to(featwin_std(fw), sqrt(512.0 * 513.0 / 12.0), 1e-6, "wrap std");
    featwin_destroy(fw);
}

/* Rolling sums drift over long runs must stay bounded: alternating huge
 * magnitudes would poison naive accumulators; periodic recompute does not. */
static void test_drift_bound(void){
    FeatureWindow *fw = featwin_create();
    for(int i = 0; i < 2048; i++)
        featwin_push(fw, (i % 2) ? 1e6 : -1e6);
    assert(featwin_n(fw) == BASELINE_WINDOW_SAMPLES);
    close_to(featwin_mean(fw), 0.0, 1.0, "huge alternation mean");
    close_to(featwin_std(fw), 1e6, 1e3, "huge alternation std");
    featwin_destroy(fw);
}

static void test_saturation_and_null(void){
    FeatureWindow *fw = featwin_create();
    for(int i = 0; i < BASELINE_WINDOW_SAMPLES * 4; i++)
        featwin_push(fw, 1.0);
    assert(featwin_n(fw) == BASELINE_WINDOW_SAMPLES);  // saturates, O(window)
    featwin_push(NULL, 1.0);                            // defensive no-crash
    assert(featwin_n(NULL) == 0);
    featwin_destroy(fw);
    featwin_destroy(NULL);
}

int main(void){
    test_empty();
    test_exact_stats();
    test_constant_std_zero();
    test_eviction();
    test_step_change_rearms();
    test_wrap_exactness();
    test_drift_bound();
    test_saturation_and_null();
    printf("test_baseline: PASS\n");
    return 0;
}
