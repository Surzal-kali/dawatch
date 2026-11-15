# Engine TODO

The engine is the C core for shared contracts, sampling control, and
cross-lane fusion. Python owns orchestration and top-level integration.

## First milestone

- [x] Define versioned C contracts for `WindowFeatures`, `Score`,
      `PoCDescriptor`, `Route`, contact reports, and burst requests.
- [ ] Define ownership, lifetime, serialization, and error semantics for every
      contract crossing the C/Python boundary.
- [x] Implement the cheap always-on baseline sampling path.
- [x] Implement Poisson-distributed random sampling.
- [x] Implement event-triggered sampling from baseline deviation.
- [x] Add a bounded retained high-resolution ring-buffer interface for
      retroactive burst promotion.
- [x] Implement per-lane contact ingestion without retaining traffic-volume
      data in the monitor.
- [ ] Implement cross-lane correlation over contacts and time.
- [ ] Produce a per-channel footprint plus cross-channel correlation suitable
      for `WindowFeatures`.
- [ ] Add Python bindings for the contracts and controller operations.
- [ ] Add focused C tests for bounds, timing, promotion, correlation, and
      malformed inputs.
      (Progress 2026-11-14: `make test` exists — test_ringbuffer (eviction,
      wrap correctness, bounded memory), test_ingest (counters at saturation,
      validation, canary split routing, stream framing on a socketpair).
      Timing/promotion/correlation tests still open.)
- [ ] Add a Python executable demo that runs the loop through one sampling
      cycle and produces a route.

### Detection correctness (acceptance criteria from the Nov 13 live matrix)

- [x] Split canary alerts out of the K-sigma scalar: canary-lane contacts route
      to `ROUTE_ALERT` directly (zero-FP lane per main README §6); the baseline
      stays clean for genuine wire/endpoint deviation. Acceptance: a single
      synthesized canary touch fires a route with k=3.0 intact. Ref: canary z
      is pinned ~1.4-1.8 by self-dilution when folded into the fused scalar
      (x = base + delta*100) — verified live, threshold cannot fix it.
      (2026-11-14 landed: `ingest_contact(..., external=true)` on the canary
      lane routes `ROUTE_ALERT` outside the fused scalar; the fused x is now
      genuine wire/base activity only — the rb_count-derived canary delta is
      gone. Acceptance met: canary_client's single touch →
      `[ROUTE] kind=2 reason=canary touch` with k=3.0 intact; smoke greps it.)
- [ ] Add the third cadence tick: Poisson-drawn mandatory canary-inspection
      (`next_canary_tick_ms`) alongside baseline/random ticks; a real touch
      still promotes immediately and separately. Acceptance: demo run emits
      all three trigger types at least once; intervals show no fixed rhythm.
- [x] Windowed baseline stats: replace the cumulative Welford (var climbs
      monotonically — ~58k by tick 30 in the stock run) with sliding-window or
      EWMA. Acceptance: sensitivity at t=X not weaker than t=X/10.
      (2026-11-14 landed: `engine/baseline.{c,h}` — sliding window of the
      last 512 fused feature samples, rolling sums re-derived once per
      capacity-worth of pushes to bound drift; sample-variance std printed
      and scored, no lifetime accumulation anywhere. Chose slide over
      discrete windows/EWMA deliberately: the ring shifts every push, so
      there is no rotation boundary or update tempo for an adversary to time
      against (§9), and state is O(window) per the memory doctrine. Window
      size is a documented horizon knob (`BASELINE_WINDOW_SAMPLES`), not a
      magic fit. Acceptance shown: [BASELINE] var now settles (~34 by n=47)
      instead of climbing; events fired at z≈4-6 throughout a 30s run —
      8 of them, late ones just as strong as early ones. Smoke covers the
      line; `engine/test_baseline.c` pins exactness/eviction/drift.)
- [x] Per-lane monotonic contact counters bumped in the push path; never
      derive deltas from ring occupancy. Acceptance: counter test at ring
      saturation (push capacity x 10, deltas stay true). Ref: rb_count wraps
      every ~85 min on canary demo tempo; minutes on wire tempo.
      (2026-11-14 landed: `IngestCounters.ingress[]` lifetime totals bumped
      in the push path, single source `engine/ingest.c`. Acceptance met:
      test_ingest pushes capacity×10 — ingress stays true at 640 while
      rb_count saturates at 64.)
- [ ] Warm-up honesty: while `baseline_n < 30`, emit a "detectors off —
      warming" line (or scale the gate by wall time). Acceptance: a 1000ms-tick
      30s run visibly says detection was dormant instead of printing
      `event=0` silently. The shipped 1000ms config ate all 30 ticks in the
      gate and nobody could tell from the output.
- [x] Ingest hardening: check `rb_push` returns; accept all pending
      connections per loop pass (currently max 1 per pass, 5ms sleep between).
      (2026-11-14 landed: `rb_push` return checked → `dropped` counter; all
      pending connections drained per pass; full-report stream framing via
      `ingest_read_contact` — partial reads retry, short stream is malformed
      and never treated as a report; lane/payload_len validated, violators
      counted as `malformed`. Tests: test_ingest framing + validation.)
- [x] `payload_len` discipline: readers slice to `payload_len`; emitters set
      it to bytes actually packed (currently 128 written vs 64 declared vs
      ~10 real — decorative on every path).
- [x] Pin the wire contract before any Python binding: fixed-width fields
      largest-first or explicit packing, `static_assert(sizeof(ContactReport)
      == N)` + endianness note (same lesson as `Score.reserved[3]`).
      (Build-time guards added in `contracts.h`: all seven struct sizes
      pinned with typedef-array asserts — ContactReport=144, Score=8,
      WindowFeatures=72, PoCDescriptor=136, Route=16, BurstRequest=32,
      EngineConfig=32 — plus endianness/payload_len on-wire conventions.
      Python bindings remain gated on the ownership/lifetime item above.)
- [ ] Swappable PRNG: seedable state behind an interface (demo `rand()` is
      fine; §9 tempo-poisoning work becomes a one-line swap; unifies the two
      U-draw idioms — engine.c line 73 guards div-zero, line 115 doesn't).

## Later phases

- [ ] Add MITRE ATT&CK Data Sources as the sensor/channel reference map.
- [ ] Evaluate per-lane model weights versus one shared scorer with
      per-lane feature heads.
- [ ] Add policy/configuration for memory budgets and lane priorities.
- [ ] Define the production deployment and isolation boundary for the C core.
