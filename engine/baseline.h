#pragma once
#include <stddef.h>

/*
 * Sliding feature window for the baseline (engine.README matrix:
 * "Windowed baseline stats"). Replaces the cumulative Welford: the baseline
 * mean/std now describe only the last N fused feature samples, so a step
 * change in lane behavior re-arms detection instead of being averaged into
 * eternity.
 *
 * Design notes:
 *  - N is a documented tuning knob ("effective memory horizon"), not a
 *    magic fit: too small and the baseline chases noise, too large and it
 *    reacts slowly. No goldilock exists; 512 samples at the demo's 200ms
 *    baseline tick ≈ 1.7 min of memory.
 *  - Sliding, not rotating: the ring shifts every push, so there is no
 *    reset boundary or update tempo an adversary could learn or time
 *    against (main.README §9 — no fixed rhythms).
 *  - O(window) memory, O(1) push (rolling sums), bounded drift by
 *    recomputing the sums from the ring once per capacity-worth of pushes.
 */

#define BASELINE_WINDOW_SAMPLES 512  /* effective memory horizon, in samples */
#define BASELINE_WARMUP_SAMPLES 30   /* scoring gated below this n (unchanged) */

typedef struct FeatureWindow FeatureWindow;

FeatureWindow *featwin_create(void);
void featwin_destroy(FeatureWindow *fw);

/* Push one sample; oldest is evicted automatically once full. */
void featwin_push(FeatureWindow *fw, double x);

/* Count of live samples in the window (min of pushes, capacity). */
size_t featwin_n(const FeatureWindow *fw);

/* Mean over the live window; 0.0 while empty. */
double featwin_mean(const FeatureWindow *fw);

/* Sample std (n-1 denominator) over the live window; 0.0 while n < 2. */
double featwin_std(const FeatureWindow *fw);
