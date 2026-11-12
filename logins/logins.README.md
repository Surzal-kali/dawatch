# Logins lane TODO

This lane captures authentication and identity activity, where low event
volume can carry high semantic weight.

## First milestone

- [ ] Define the C-facing event shape for logons, logoffs, failures,
      privilege changes, and lateral-movement-relevant identity actions.
- [ ] Define the minimum identity metadata needed for a `WindowFeatures`
      footprint while avoiding unnecessary sensitive payloads.
- [ ] Implement a cheap baseline collector for authentication activity.
- [ ] Add a bounded high-resolution ring buffer for recent identity events.
- [ ] Emit compact contact reports to the engine.
- [ ] Add focused tests for normalization, repeated failures, privilege
      changes, ordering, and ring-buffer eviction.
- [ ] Add a small executable demo that produces a contact report from sample
      authentication events.

## Later phases

- [ ] Map supported identity activity to the relevant MITRE ATT&CK Data
      Sources.
- [ ] Add burst promotion when the engine requests a historical identity
      window.
- [ ] Define privacy-preserving handling for identity data in logs, buffers,
      and Python-facing interfaces.