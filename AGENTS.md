# AGENTS.md — operating notes for coding agents on dawatch

dawatch is a blue-team framework: an ASW-style detection engine in C
("hall monitor") that fuses compact contact reports from per-lane sensors
(wire, endpoint, auth, API, canary/honeyjars) over a Unix socket, with tiered
sampling — always-on K-sigma baseline, Poisson random bursts, event-triggered
bursts — and active deception (honeynet) as a later phase. Python
orchestration/bindings and the models lane come after ingest correctness.

## Language mix and sequencing

- **C stays the engine of record** (contracts, sampling control, fusion).
- **Python** arrives as orchestration + bindings (engine first-milestone
  items) only after the detection-correctness matrix lands — bindings freeze
  the ingest path, and the ingest path is mid-surgery. The remaining C-side
  gate is the ownership/lifetime/serialization/error-semantics item in
  engine.README.
- **PowerShell** is the planned Windows endpoint collector, produced as a
  lane emitter of `ContactReport`s. It is gated on a decision not yet made:
  ingest is same-host `AF_UNIX` today, so remote lanes need framed,
  authenticated cross-host transport (see endpoints.README later phases).


## Read first — source of truth, in order

1. **`main.README.md`** — the design (§3 sampling controller, §5 lanes +
   hall monitor, §9 recurring motifs) and §11 checklists (reproducibility
   invariant, build+smoke, lane conformance).
2. **Per-lane checklists** — each folder's `*.README.md` is that lane's live
   TODO list with acceptance criteria:
   `engine/`, `honeyjars/`, `endpoints/`, `apis/`, `logins/`, `models/`,
   `wires/`.
3. **`engine/contracts.h`** — the pinned wire contract; its header comment
   documents on-wire conventions before you touch any struct.

Checklists state status as `[ ]`/`[x]` with greppable acceptance criteria.
Trust them over chat prose or remembered context: an item is done only when
its criteria are demonstrable in the repo.

## Build + verify

- `make all` — `-Wall -Wextra`; warning-free is the bar.
- `make smoke` — one pass: runs the 30s engine demo in the background, fires
  the canary client at it, then greps the log for each deterministically
  fireable trigger (baseline ticks, random burst, socket ingestion) and the
  clean-finish line. Must be red/green on a clean clone before engine work
  is considered done.
- `make clean` — binaries live at the .gitignore'd paths
  (`engine/engine_demo`, `honeyjars/canary_client`, `honeyjars/canary_demo`).
- Focused C tests (plain asserts, a `make test` target once tests exist) —
  no test framework; keep it gcc + make only.

## Hard rules (rationale in main.README §9–§11)

- **Lane conformance bar** (main.README §11) applies to any lane code or
  engine ingest: truthful `payload_len` (bytes actually packed, never past
  the 128-byte bound); lifetime ingress counters — never derive deltas from
  `rb_count` (it wraps at saturation); ring-buffer eviction/wrap/bounded-
  memory tests; lane demos proven end-to-end against a live engine socket;
  raw-content prohibitions (raw packets, PII, real creds, unrestricted
  egress) enforced as negative tests, never just comments.
- **Contract asserts fail on purpose.** `contracts.h` pins struct sizes
  (ContactReport=144, Score=8, WindowFeatures=72, PoCDescriptor=136,
  Route=16, BurstRequest=32, EngineConfig=32). Changing a contract struct
  means bumping `BLUEC_CONTRACT_VERSION`, re-deriving the sizes, and noting
  endianness implications. Never "fix" an assert failure by matching
  whatever size the compiler reports.
- **Bound everything by window, not volume.** Ring buffers are O(window),
  the monitor is O(contacts); nothing may scale with traffic except buffer
  aging.
- **Poisson, not fixed-N, for every rhythm** — random sampler, synthetic
  liveness, inspections. Fixed tempos are exploitable.
- **Tick checklists honestly and together.** Land the code/test satisfying
  the criteria and tick the item in the same change. Checklists are the
  externalized memory for memoryless sessions; edits without the criteria
  landed are drift, not progress.

## Where the work is (update this on session close)

- Done (2026-11-14, tier 1 reproducibility): Makefile + smoke, wire-contract
  pinning, deletion of `engine_skeleton.c`/`core.cpp`, demo λ raised for
  smoke determinism. See §11 ticks.
- Done (2026-11-14, ingest first sweep): per-lane lifetime ingress counters +
  ingest hardening + canary split (`ROUTE_ALERT` for external canary touches)
  landed together via `engine/ingest.{c,h}`; `make test` now runs
  ring-buffer + ingest suites; smoke covers every implemented trigger
  including the event line. See §11 + engine.README ticks.
- Next: remaining "Detection correctness" matrix items — windowed baseline
  stats (cumulative Welford variance climbs monotonically), warm-up honesty
  line, canary cadence tick, swappable PRNG. Then lane build-out, wire lane
  first (flow-feature shape → contact emission → ring buffer → demo →
  tests, per the §11 lane conformance bar).
