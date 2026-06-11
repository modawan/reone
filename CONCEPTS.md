# Concepts

Shared domain vocabulary for this project — entities, named processes, and status concepts with project-specific meaning. Seeded with core domain vocabulary, then accretes as ce-compound and ce-compound-refresh process learnings; direct edits are fine. Glossary only, not a spec or catch-all.

## Resource loading

### Installation

The game-root indexer and resolver for KotOR assets. It lazily loads chitin, override trees, module capsules, texture packs, streams, and save-scoped custom folders/capsules, then answers lookups through ordered search locations.

### SearchScope

An ordered list of search locations consulted until a resource is found. Different resource kinds use different scopes (canonical, texture, sound, talk table, movie).

### Module root

The active module name used to filter which module archives participate in lookup. Until set, module-scoped resources are not indexed — matching PyKotor lazy module indexing.

### Global scope

Installation-wide custom folders and capsules (for example shader-pack assets) established during resource director init. Save-scope resets restore to this baseline.

### Save scope

The transient custom folders and capsules pointing at a save directory and its `savegame.sav`. Established by the resource director on game load and must survive module transitions while a save is active. Resetting save scope must also drop any cached lazy capsule indexes for the prior save file.

### Location result

A resolved asset reference: file path, byte offset, size, and optional nested read helpers. The Installation layer returns these; the Resources facade wraps them as in-memory `Resource` blobs.

## Downstream integration

### Stacked modawan PR

One of three sequential downstream pull requests to modawan/reone (resource loader, then GLES, then WASM) replacing the closed #111 monolith. Each slice is path-filtered from OpenKotOR `master`, not a replay of the monolith commit history.
