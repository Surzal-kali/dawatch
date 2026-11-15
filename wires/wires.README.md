# Wires lane TODO

This lane observes network metadata and tempo without retaining raw packets.

## First milestone

- [x] Define the C-facing flow-feature shape for timing, volume, direction,
      cadence, protocol/TLS fingerprints, and endpoint identities.
      (2026-11-14: `wire_flow.h` — `WireFlow` 48B static-asserted: interval
      endpoints, pkt/byte counts by direction, protocol class, `dst_port`,
      salted `peer_token` (FNV-1a, 0=untracked). TLS fingerprints deferred to
      later phases.)
- [x] Define the minimum wire metadata needed for a `WindowFeatures` footprint.
      (2026-11-14: window tick maps tempo mean/std + flow count →
      `wire_tempo_mean/std`, `wire_flow_count`; byte splits ship per-tick.)
- [ ] Implement a cheap continuous flow-feature collector.
      (Progress 2026-11-14: `wire_source.h` pull-style backend interface +
      seeded synthetic Poisson backend landed; real /proc/pcap-class taps are
      the open remainder of this item.)
- [x] Ensure the lane buffers flow features rather than raw packets.
      (2026-11-14: source contract is reduce-and-discard; the type system
      cannot express raw packets; negative tests assert no raw identifier /
      credential substrings in any contact payload.)
- [x] Add a bounded high-resolution ring buffer for recent flow features.
      (2026-11-14: `wire_monitor.c` reuses `engine/ringbuffer.c` under the
      monitor, cap default 512; test asserts flow_count stays at cap under
      10× overflow and window aggregation is independent of eviction.)
- [x] Emit compact contact reports to the engine.
      (2026-11-14: three kinds — FLOW_TICK 40B / BEACON_SUSPECT 36B /
      NEW_PEER 20B, explicit pad bytes, truthful `payload_len`, bound-never-
      crossed proven by sentinel test; demo streams them over the engine's
      ingest path, lane=0.)
- [x] Add focused tests for aggregation, cadence, directionality, memory bounds,
      and ring-buffer eviction.
      (2026-11-14: `wires/test_wire.c` — plain asserts, in `make test`:
      tokenization stability/salting, window mean/std/counts + direction split
      + reset, beacon fires-once-per-episode + re-arm + jitter-stays-quiet,
      capacity bounds, OOO-timestamp safety, payload-bound + raw-content
      negative tests.)
- [x] Add a small executable demo that produces a contact report from sample
      flow metadata.
      (2026-11-14: `wires/wire_demo.c` — seeded synthetic feed (chatter +
      700ms beaconer), one persistent connection streaming contacts into the
      live engine socket; `make smoke` runs it and greps lane=0 ingest plus
      FLOW_TICK / BEACON_SUSPECT emission.)

## Later phases

- [ ] Map supported network activity to the relevant MITRE ATT&CK Data
      Sources.
- [ ] Add burst promotion when the engine requests a historical wire window.
- [ ] Evaluate format-preserving tokenized replay versus synthesis for wire
      deception telemetry.
- [ ] Document the explicit prohibition on raw-packet retention in the
      deception path.