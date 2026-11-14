# Copilot instructions (dawatch)

Authoritative sources, in order: `AGENTS.md` (operating notes),
`main.README.md` (design + §11 checklists), and each folder's `*.README.md`
lane checklist. Checklists are the live status with greppable acceptance
criteria — trust them over chat prose or memory.

## What this repo is

Blue-team framework: a C engine ("hall monitor") fusing per-lane sensor
contact reports (wire, endpoint, auth, API, canary/honeyjars) over a Unix
socket, with tiered sampling — always-on K-sigma baseline, Poisson random
bursts, event-triggered bursts. Honeynet/deception, Python bindings, and the
models lane are later phases gated on ingest correctness. Language mix: C is
the engine of record; Python (orchestration + bindings) comes after the
detection-correctness matrix; PowerShell is the planned Windows endpoint
collector, gated on a remote-safe transport decision (ingest is same-host
AF_UNIX today).

## Hard rules (violations break the build or the design)

- `engine/contracts.h` struct layout is pinned by static asserts
  (ContactReport=144, Score=8, ...). Deliberate contract changes mean bumping
  `BLUEC_CONTRACT_VERSION` and re-deriving asserts; never silence a failing
  assert by matching the reported size.
- `payload_len` must be bytes actually packed; never write past the 128-byte
  payload bound.
- Never derive engine deltas from `rb_count` — it wraps at saturation; use
  lifetime ingress counters.
- Bound memory by window (O(window) buffers, O(contacts) monitor), never by
  traffic.
- All tempos Poisson, no fixed rhythms an adversary could learn.
- No raw packets, PII, real credentials, or unrestricted egress in lanes,
  demos, or decoys; prohibitions become negative tests, not comments.
- New lane or ingest code must meet the conformance bar in main.README §11.

## Build + verify before claiming done

- `make all` must be warning-free under `-Wall -Wextra`.
- `make smoke` must pass (30s engine demo + canary client + trigger greps;
  red/green on a clean clone).
- Tick checklist items only in the change that satisfies their criteria.

## Current next steps (as of 2026-11-14)

`engine/engine.README.md` "Detection correctness" matrix — first sweep:
per-lane ingress counters + ingest hardening + canary split (same ingest
path). Then windowed baseline stats, warm-up honesty, canary cadence tick,
swappable PRNG. Event-trigger smoke coverage is intentionally absent until
the canary split lands (self-dilution pins fused z below k=3.0).
