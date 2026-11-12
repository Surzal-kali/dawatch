# APIs lane TODO

This lane captures cloud and control-plane activity without owning cross-lane
correlation or routing.

## First milestone

- [ ] Define the C-facing contact-report shape for IAM actions, configuration
      changes, and other API activity.
- [ ] Define the minimum API metadata needed for a `WindowFeatures` footprint:
      actor, target, action, result, timestamp, and source context.
- [ ] Implement a cheap baseline collector for API activity.
- [ ] Add a bounded high-resolution ring buffer for recent API events.
- [ ] Emit compact contact reports to the engine instead of raw event streams.
- [ ] Add focused tests for normalization, timestamp ordering, and ring-buffer
      eviction.
- [ ] Add a small executable demo that produces a contact report from sample
      API events.

## Later phases

- [ ] Map supported API activity to the relevant MITRE ATT&CK Data Sources.
- [ ] Add burst promotion when the engine requests a historical API window.
- [ ] Define synthetic API behavior for the deception environment.