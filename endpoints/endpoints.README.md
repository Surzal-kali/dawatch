# Endpoints lane TODO

This lane captures high-fidelity host activity, including what actually
executed, while keeping local buffering and lane-specific processing local.

## First milestone

- [ ] Define the C-facing event shape for process trees, command lines, module
      loads, and other discrete endpoint events.
- [ ] Define the minimum endpoint metadata needed for a `WindowFeatures`
      footprint.
- [ ] Implement a cheap baseline collector for endpoint events.
- [ ] Add a bounded high-resolution ring buffer for recent endpoint events.
- [ ] Emit compact contact reports to the engine.
- [ ] Add focused tests for parent/child process relationships, normalization,
      ordering, and ring-buffer eviction.
- [ ] Add a small executable demo that turns sample process activity into a
      contact report.

## Later phases

- [ ] Map supported endpoint activity to the relevant MITRE ATT&CK Data
      Sources.
- [ ] Add burst promotion when the engine requests a historical endpoint
      window.
- [ ] Define the containment and telemetry requirements for a reactive decoy
      endpoint.