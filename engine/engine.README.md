# Engine TODO

The engine is the C core for shared contracts, sampling control, and
cross-lane fusion. Python owns orchestration and top-level integration.

## First milestone

- [ ] Define versioned C contracts for `WindowFeatures`, `Score`,
      `PoCDescriptor`, `Route`, contact reports, and burst requests.
- [ ] Define ownership, lifetime, serialization, and error semantics for every
      contract crossing the C/Python boundary.
- [ ] Implement the cheap always-on baseline sampling path.
- [ ] Implement Poisson-distributed random sampling.
- [ ] Implement event-triggered sampling from baseline deviation.
- [ ] Add a bounded retained high-resolution ring-buffer interface for
      retroactive burst promotion.
- [ ] Implement per-lane contact ingestion without retaining traffic-volume
      data in the monitor.
- [ ] Implement cross-lane correlation over contacts and time.
- [ ] Produce a per-channel footprint plus cross-channel correlation suitable
      for `WindowFeatures`.
- [ ] Add Python bindings for the contracts and controller operations.
- [ ] Add focused C tests for bounds, timing, promotion, correlation, and
      malformed inputs.
- [ ] Add a Python executable demo that runs the loop through one sampling
      cycle and produces a route.

## Later phases

- [ ] Add MITRE ATT&CK Data Sources as the sensor/channel reference map.
- [ ] Evaluate per-lane model weights versus one shared scorer with
      per-lane feature heads.
- [ ] Add policy/configuration for memory budgets and lane priorities.
- [ ] Define the production deployment and isolation boundary for the C core.