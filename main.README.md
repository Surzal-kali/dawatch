# Design Notes — Adaptive Self-Introspection + Active Deception

*Theoretical design sketch. Status: concept, pre-code. Context: self-analysis / detectability layer + active honeynet, sitting alongside an authorized-pentest orchestration loop.*

\---

## 0\. The one-line shape

One analysis loop with organs, not three separate projects:

```
loop runs → introspection burst samples it → decision model scores the burst → score routes the next action
         ↑\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_ hall monitor owns all of this \\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_\\\\\\\_|
```

* **Sensor** = the introspection burst (idea 3)
* **Brain** = the decision/scoring model (idea 1)
* **Memory** = the PoC store (idea 2)

They're only interesting wired together.

\---

## 1\. Boundary objects (design these first)

The whole thing lives or dies on the organs speaking the same language. Four typed messages; if these are precise, every organ becomes a function with a signature and is independently buildable/swappable (and the Rust port later is reimplementing clean contracts, not chasing tangled state).

* **`WindowFeatures`** — what the burst emits. A time-window of the loop reduced to tempo/sequence features. The hard one (see §4).
* **`Score`** — what the scorer emits. Calibrated probability (+ class if more than binary). Deliberately small and dumb.
* **`PoCDescriptor`** — normalized feature vector extracted from a PoC. This is what lives in the store, **not** the raw PoC.
* **`Route`** — what the router emits given score + match/no-match. The only organ allowed to have opinions about what happens next.

\---

## 2\. The decision model + PoC store (ideas 1 \& 2)

* Right instinct to drop the generative model: this is a **scorer, not a generator** — you want a thresholdable probability and a route, not prose.
* **Don't match raw PoCs.** Two exploits for the same CVE can look nothing alike; two for different bugs can look near-identical (shared shellcode stub, same request skeleton). Cosine similarity on raw text lies confidently.
* **Match on a normalized feature vector instead:** target service + version range, CWE class, primitive used, CVE if known, delivery vector.
* **Start dumber than you want:** retrieval + similarity threshold gives a working router before you train anything. Add the learned model exactly where the threshold starts being wrong.
* **The flywheel:** every run the harness does is a labeled example. The logs *are* the dataset.

*(Open: "clef / jev" as the decision model — need to pin down what they actually are. Architecturally the category "calibrated-probability classifier" is correct regardless.)*

\---

## 3\. The sampling controller (reframe of "the burst")

The burst isn't a component — it's the **expensive mode of a sampling controller** that also runs a cheap mode and owns the triggers. Don't design "the burst"; design the controller.

**Two resolution tiers:**

* **Baseline** — continuous, cheap, always-on. *Also does a second job:* it's the reference distribution that **defines what counts as an event** (an event trigger = "baseline says this window is K-sigma out"). So it can't just be cheap — it has to be statistically meaningful enough for "deviation" to mean something. That tension (cheap enough to always run, rich enough to define normal) is the thing to design around.
* **Burst** — hyper-focused, expensive, high-resolution. Triggered only.

**Two triggers:**

* **Event-based** — fires burst on baseline-flagged deviation. Can only ever over-analyze things you already taught it to recognize.
* **Random** — NOT a fallback. It's the **unknown-unknown detector**: it covers the empty water where the quiet threat hides, because event triggers are blind to signatures they don't know. (ASW: you prosecute known datums *and* run search patterns over empty ocean.)

**Two load-bearing knobs:**

* **Make the random sampler Poisson, not fixed-N.** Fixed "every N since last event" has predictable, exploitable gaps. Memoryless intervals don't. Recursion: a *detectability* analyzer shouldn't itself run on a detectable tempo.
* **Retained high-res ring buffer, or event-bursts can only look forward.** When an event fires, the interesting part is usually the lead-up, which already happened. Continuously buffer fine-grained telemetry so you can **retroactively promote** a window. Cheap to design in now, miserable to retrofit. Keeps "over-analyze" honest = read deeper into what you already captured, not go probe more (passive, not active — avoids the observer effect contaminating your own tempo signal).

\---

## 4\. The five ingestion lanes ("the other side of the glass")

A SOC fuses multiple sensors; so must your self-model. ASW framing because it's structurally correct (manage a signature across sensors over time):

|Lane|ASW analog|Character|
|-|-|-|
|**Wire**|passive sonar|High volume, low detail (mostly metadata post-encryption: timing, volume, beacon cadence, TLS fingerprints). **It's mostly tempo already.**|
|**Endpoint**|hull contact / MAD|Lower volume, high detail, discrete events (process trees, cmdlines, module loads). Highest fidelity; sees what actually *executed*.|
|**Auth / identity**|ESM|Low volume, high semantic weight. Lateral movement + privilege live here. Modern detection leans on it hard.|
|**Cloud / API**|control plane|IAM actions, config changes. Where *impact* happens.|
|**Canaries / honeynet**|active sonar ping|Near-zero volume, binary signal. A touch IS the alert. Highest signal-to-noise of all.|

**The fusion punchline (reshapes `WindowFeatures`):** no single sensor catches the sub — the hunter/SOC correlates contacts across modalities and time. **Simultaneity across channels is itself the loudest signature.** So a window isn't one tempo signal — it's a **per-channel footprint vector + the cross-channel correlation.** A loop can be whisper-quiet on the wire and screaming on the endpoint in the same burst.

**Reference map to build against:** MITRE ATT\&CK **Data Sources** taxonomy — enumerates sensor channels (Process, Network Traffic, Logon Session, File, Cloud Service…) and ties each to the techniques it can see. It's the defender's sensor manifest, written down.

\---

## 5\. Per-lane engines + the "hall monitor"

Each lane gets its own analysis engine and tempo (the lanes have genuinely different statistics, so a one-size analyzer is wrong for all of them), with a central **hall monitor** over the top.

* The per-lane engines = **sensor-local processing.**
* The hall monitor = **the fusion/correlation layer** (SIEM/UEBA brain; ASW combat system). **It IS the sampling controller from §3, scaled to multiple inputs** — the design cohered on its own.

**The memory trick lives at the lane→monitor interface:**

* Lanes keep their high-res ring buffers **locally** and ship the monitor only **compact contact reports** ("saw something at T, here are five numbers").
* Monitor holds **O(contacts), not O(traffic)** — footprint scales with how much is *interesting*, not how much is *flowing*.
* When the monitor spots a cross-lane pattern worth a burst, it reaches back **down** and tells those lanes to promote their buffered window. Detail stays at the edge until summoned.
* A ring buffer is **O(window), not O(events)** — a traffic spike just ages buffers faster, it doesn't grow them. Nothing in the design is allowed to scale with volume except the thing that's cheap to age out.

\---

## 6\. Priority = where you spend the memory budget

Split "endpoint + API highest" across two axes, because the axes are literally the spend decision:

* **High weight, low cost — canaries + auth/identity.** Cheap to watch, huge payoff. **Fund fully.**
* **High weight, medium cost — endpoint.** Where execution lands. Worth the spend.
* **Low weight-per-event, high volume — the wire.** Wants to eat all your memory, deserves the least per byte. **Buffer flow features, never raw packets** — probably your single biggest memory lever.

Net: the feasibility fight is almost entirely the wire lane; the rest are cheap.

**Consolidation fork if N resident engines is too heavy:** keep per-lane *state and tempo* (baselines, trigger timers — small), but ask whether lanes truly need separate *model weights*. One shared scorer with per-lane feature heads keeps the behavior while paying for the model once. That's usually where the real memory sits.

\---

## 7\. Active honeynet ("living, breathing")

**The problem spotted:** passive honeypots have a liveness tell — a patient profiler notices the thing never *does* anything. So you want simulated life. But that costs more footprint, and risks leaking real data.

**Prior art you re-derived:**

* "Lead them down the right path" = **breadcrumbing** — seed lures on *real* assets (fake creds in a config, planted API key, decoy mapped drive) that natural recon follows *into* the deception.
* The commercial deception-grid category runs on exactly this instinct.

**Core move — don't run a living environment, emit the telemetry one would:**

* The attacker profiling your decoy reads the same five lanes you're modeling. The decoy doesn't need to *be* a busy server, it needs to *exhaust like one*.
* **The baseline distributions, run in reverse, are a generator.** Footprint drops from "a real datacenter" to "a handful of distribution parameters + a scheduler."

**Two tells to beat (both recur):**

* Synthetic activity is too regular → **Poisson it** (same fix as the sampler).
* Synthetic activity doesn't *respond* → a tape loop reads as alive until the attacker touches something and the world doesn't react. Holding coherent reactive state is the expensive part.

**Resolution = the controller pattern pointed outward:**

* **Cheap synthetic-baseline generator, always on** (passive liveness, tiny).
* **Expensive high-interaction reactive mode, spun up only when a canary actually fires.** You don't pay the interaction cost until someone's in the trap — and by then you already have your zero-false-positive alert, so it's always worth it.

**Containment discipline (the boring-but-mandatory part):** a high-interaction decoy is a real-ish box an attacker interacts with — weak containment turns your trap into their staging server ("blue door / red door"). Strict **data + egress control**: they can get in, trip every wire, but the environment physically cannot reach production or the outside. "On top of" must mean **segmented and instrumented**, never *adjacent*.

\---

## 8\. Replay vs. synthesis for decoy telemetry

**The realization:** needing to *censor* replayed prod traffic is the signal that **replay is the wrong primitive.** Scrubbing real prod before it enters a box an attacker sits in to read it = an arms race between your redactor and their harvester, and you only have to lose once (one cred, one token, one PII row = breach).

**Resolution — deception is an interface property, not content identity:**

* Target is **behavioral indistinguishability with zero content fidelity** — acts like prod, contains nothing of prod. Decouple shape from payload.
* Ingested prod traffic = **training data for the generator, discarded after.** Keep timing/size distributions, protocol mix, who-talks-to-whom graph, logon cadence. Generate fresh traffic stamped *now* (also kills the staleness tell — replayed 2-day-old data quotes a frozen world; synthesis is always live-dated).
* **Two clean pipelines:** behavior *learned* from prod; payload *authored* as traps (honeytokens — fake creds that look real, resolve to nothing, scream when touched). Never "real content minus the scary bits."

**Fallback for lanes miserable to synthesize (deep wire protocol structure especially):** **format-preserving tokenization** — swap every sensitive value for a same-shape fake, *consistently* (same real user → same fake user), so behavioral correlations survive but no value is ever a real identity.

**Rule of thumb:** synthesize where you can, tokenize-replay where you can't, raw-replay nothing. **The wire is the one lane where tokenized replay may genuinely beat synthesis** (easiest to copy for generation).

\---

## 9\. Recurring design motifs

* **Detector ↔ generator duality.** The model that scores "how alive / how detectable" is the same model that can *produce* believable life, run in reverse. Shows up for the detectability estimator (inverts to a behavioral detector = the blue artifact) and the honeynet (inverts to a liveness generator).
* **Match the sensor's resolution.** You only need to beat the resolution the threat actually operates at (dwell-time profiler looks at aggregate liveness, not forensic packet diffs).
* **Poison your tempo.** Anything with a rhythm an adversary can learn is a liability — the sampler, the honey-user activity, all of it.
* **Bound everything by window, not volume.** Ring buffers O(window); monitor O(contacts). Nothing scales with traffic.
* **Tiered resolution gated by triggers.** Cheap always-on + expensive on-demand, paid only when something's actually there. Reused by the sampler AND the honeynet.

\---

## 10\. Open threads / next

* Sketch the actual `WindowFeatures` columns — where tempo intuition becomes columns (per-lane footprint vector + cross-channel correlation).
* Which organ to build first? (Argument for the introspection burst: without the sensor the others have nothing to eat.)
* Sketch the hall-monitor state (small — contacts, correlation hypotheses, global tempo policy). Naming it tells you how much memory it actually carries.
* Per-lane model vs. shared scorer with per-lane heads — decide once memory budget is real.
* Honeynet: which tier to start on — baseline generator vs. reactive interaction.
* Confirm what "clef / jev" are and whether they fit the `Score` contract.

\---

## 11\. Build + docs tier (for local-model sessions)

These checklists are the repo's externalized memory for memoryless coding
sessions — a request here never starts from scratch. Keep items anchored to
greppable identifiers with red/green acceptance criteria; write expiring lore
as criteria, not prose.

### Shipped-demo reproducibility invariant

- [ ] The committed demo config must demonstrably fire every implemented
      trigger type within one run window. (History: the original 1000ms /
      30-tick config shipped with the canary pop impossible — the `baseline_n
      < 30` warm-up gate ate all 30 ticks and nothing in the output revealed
      it; the earned tuning lived in chat, not the repo. `32fa247` fixed the
      instance; this item makes it a rule.)

### Build + smoke

- [ ] Makefile: `-Wall -Wextra -lm`; targets `engine`, `canary_client`,
      `canary_demo`, `smoke` (demo + client in one pass; grep the log for
      each trigger line). Acceptance: `make smoke` red/green on a clean clone.
- [ ] Delete `engine/engine_skeleton.c` (superseded by `engine.c`; duplicate
      baselines diverge).
- [ ] Seed the doc tier: an `AGENTS.md`-lite / `.github/copilot-instructions.md`
      pointing at main.README, the per-lane checklists, and the lane
      conformance block below.

### Lane conformance (the shared bar; all five lanes + engine ingest)

- [ ] Contact emission sets `payload_len` truthfully, never writes past the
      bound; the engine slices on it. (Currently decorative on every path.)
- [ ] Contacts ship lifetime ingress counters; the engine never derives
      deltas from ring occupancy (`rb_count` wraps at saturation — wire lane
      wraps in minutes).
- [ ] Ring-buffer tests: eviction at saturation, wrap correctness, bounded
      memory (capacity stays capacity — O(window), never O(events)).
- [ ] Lane demos run end-to-end against a live engine socket: emit →
      ingest → `rb_count` increments observed in the engine log.
- [ ] Raw-content prohibitions (raw packets, PII payloads, real creds)
      enforced as negative tests, not comments.

\---

*Two-week reality check: full thing, done well, is more than two weeks. Lock the spine — loop + introspection burst + thresholded router over a small feature-extracted PoC store. Learned decision model + detector-inversion + full active honeynet are stretch.*

