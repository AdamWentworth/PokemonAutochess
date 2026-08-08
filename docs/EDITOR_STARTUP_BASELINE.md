# Editor startup baseline

Status: Active
Type: Assessment
Last updated: 2026-08-02

Measured on 2026-08-02 with the Debug D3D12 editor, a warm filesystem cache,
the tracked `phlosion.project.json`, and `--frames=2`.

| Scenario | Before housework | Current |
| --- | ---: | ---: |
| Open project for Scene editing | 25.25 s | 6.72 s median |
| Renderer initialization | about 0.18 s | about 0.18 s |
| Explicit game-preview warmup | eager/included above | 12.70 s on demand |

The ordinary editing path improved by about 73%. The change comes from:

- deferring `GameRuntime` construction and model/VFX prewarming until Game
  Preview is requested;
- removing redundant full layout composition before the authored scene is
  applied;
- optimizing container decode and Route 1 composition hot paths in Debug;
- eliminating startup-time prefab file probes;
- logging descriptor, plugin, catalogs, scene prewarm, workspace, and total
  phases independently.

The remaining project-open cost is dominated by the 91 MB cooked Route 1
archive and two evidence-preserving scene-composition phases. A future disk
cache should store a cooked, renderer-ready scene artifact keyed by archive
content hash, authored-scene hash, board-layout hash, adapter schema, and
renderer material-profile version. It must never treat the cache as authored
data or reuse it after any key component changes.
