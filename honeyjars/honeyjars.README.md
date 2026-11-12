# Honeyjars lane TODO

This lane represents canaries and honeynet/deception surfaces. A canary touch
is a high-signal contact; active deception remains a later phase.

## First milestone

- [ ] Define the C-facing canary contact-report shape.
- [ ] Define the minimum canary metadata: identifier, location, action,
      actor/context, timestamp, and validity state.
- [ ] Implement canary event normalization and compact contact emission.
- [ ] Add focused tests for valid touches, duplicate events, stale events, and
      invalid canary identifiers.
- [ ] Add a small executable demo that emits a contact when a sample canary is
      accessed.
- [ ] Document that canary data must not contain real credentials, tokens,
      production data, or unrestricted egress.

## Architecture and research TODOs

- [ ] Define the cheap synthetic-baseline generator for passive liveness.
- [ ] Define the trigger and lifecycle for high-interaction reactive mode.
- [ ] Specify strict data and egress containment before any interactive decoy
      implementation.
- [ ] Compare synthesis with format-preserving tokenized replay for each lane.
- [ ] Define authored honeytokens that resolve to nothing and alert on use.
- [ ] Document how decoy telemetry remains live-dated and behaviorally
      plausible without replaying raw production content.