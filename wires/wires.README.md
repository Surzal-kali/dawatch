# Wires lane TODO

This lane observes network metadata and tempo without retaining raw packets.

## First milestone

- [ ] Define the C-facing flow-feature shape for timing, volume, direction,
      cadence, protocol/TLS fingerprints, and endpoint identities.
- [ ] Define the minimum wire metadata needed for a `WindowFeatures` footprint.
- [ ] Implement a cheap continuous flow-feature collector.
- [ ] Ensure the lane buffers flow features rather than raw packets.
- [ ] Add a bounded high-resolution ring buffer for recent flow features.
- [ ] Emit compact contact reports to the engine.
- [ ] Add focused tests for aggregation, cadence, directionality, memory bounds,
      and ring-buffer eviction.
- [ ] Add a small executable demo that produces a contact report from sample
      flow metadata.

## Later phases

- [ ] Map supported network activity to the relevant MITRE ATT&CK Data
      Sources.
- [ ] Add burst promotion when the engine requests a historical wire window.
- [ ] Evaluate format-preserving tokenized replay versus synthesis for wire
      deception telemetry.
- [ ] Document the explicit prohibition on raw-packet retention in the
      deception path.