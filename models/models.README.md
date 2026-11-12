# Models TODO

This area owns the scorer, router, and shared model state used by the engine.
The first implementation is retrieval and similarity based; learned scoring is
deferred.

## First milestone

- [ ] Define the scorer interface that accepts a `WindowFeatures` value and
      returns a calibrated-probability `Score`.
- [ ] Define the binary classification contract first, with an extension point
      for multiple classes.
- [ ] Define the normalized `PoCDescriptor` fields: target service/version
      range, CWE class, primitive, known CVE, and delivery vector.
- [ ] Implement PoC feature extraction without storing raw PoC content.
- [ ] Implement bounded local PoC storage for descriptors and labels.
- [ ] Implement descriptor matching and cosine-similarity retrieval.
- [ ] Define the similarity threshold and no-match behavior.
- [ ] Define the router interface that accepts `Score` plus match/no-match and
      returns the only authoritative `Route`.
- [ ] Define shared state for thresholds, model metadata, recent decisions, and
      bounded memory usage.
- [ ] Keep `clef` and `jev` behind the scorer interface until their identities
      and fit are confirmed.
- [ ] Add focused tests for descriptor normalization, similarity thresholds,
      empty stores, score calibration shape, and route selection.
- [ ] Add a small executable demo that scores a feature window, queries the
      PoC store, and emits a route.

## Later phases

- [ ] Replace or augment retrieval with a learned calibrated classifier when
      the labeled run dataset justifies it.
- [ ] Define dataset collection, labeling, calibration, and model-versioning
      procedures.
- [ ] Evaluate one shared scorer with per-lane feature heads against separate
      per-lane model weights.
- [ ] Define safe persistence and migration for model state.
