# Renderer Configuration

Status: Active
Type: Reference
Last updated: 2026-07-22

## Purpose

This is the preferred runtime and profiling configuration for the three active
renderers. Performance modes must preserve the shared material contract:
base-color and authored PBR textures, material colors, transparency, tail fire,
and character outlines remain visual requirements rather than optional
benchmark work.

## Recommended Runtime Defaults

| Backend | Recommended use | Configuration |
| --- | --- | --- |
| Vulkan | Preferred on a capable adapter when it initializes cleanly | Leave Vulkan feature-test variables unset. Descriptor-indexed indirect world submission is selected automatically when supported, with a direct fallback otherwise. |
| D3D12 | Native Windows alternative | Use the default world-scene/GPU-skinning path. Character outline extrusion remains shader-driven. |
| OpenGL | Broad compatibility and graphics-debug fallback | Use the default shared indexed path and GPU clip skinning. Character outline extrusion remains shader-driven. |

For normal play:

- Keep VSync enabled when stable frame pacing is more important than minimum
  latency; use the uncapped mode only for profiling or when explicitly desired.
- Treat character inking as a visual preference. Enabling it must add a thin
  silhouette without replacing the textured model surface.
- Do not set benchmark or Vulkan feature-disable environment variables in a
  normal launch.
- Retain the shared 16x anisotropy, linear RGBA16F neutral PMREM, ACES tone
  mapping, and material-map defaults on every backend.

The July 22 dense-roster Release capture on the local GTX 1070 favored Vulkan,
but backend choice should still be validated on the shipping hardware set.
Vulkan initialization failure continues to use the normal OpenGL recovery path.

## Benchmark Configuration

Use a Release binary, a pinned deterministic snapshot, fixed resolution,
VSync off, FPS cap zero, and the same inking state for every compared backend.
For an inked dense-roster comparison:

```powershell
.\tools\benchmark_render_matrix.ps1 `
  -BuildDir build -Config Release -NoBuild `
  -Backends opengl,vulkan,d3d12 -Resolutions 1280x720 `
  -DurationSeconds 12 -WarmupSamples 3 -MinScoredSamples 6 `
  -SnapshotPath config/debug/debug_state_snapshot_perf_dense_roster.json `
  -AutoLoadSnapshot -PinSnapshotState `
  -VideoVsync 0 -VideoFpsCap 0 -VideoCharacterInking 1
```

The final textured July 22 checkpoint measured:

| Backend | FPS | CPU frame | Render build | GPU frame |
| --- | ---: | ---: | ---: | ---: |
| OpenGL | 658.674 | 1.562 ms | 1.305 ms | 1.311 ms |
| Vulkan | 799.748 | 1.288 ms | 0.775 ms | 0.367 ms |
| D3D12 | 404.544 | 2.514 ms | 1.350 ms | 0.842 ms |

These are local comparative measurements, not universal hardware guarantees.

## Vulkan Diagnostic Overrides

These variables exist for same-binary comparisons and fallback validation:

- `PAC_VULKAN_INDIRECT_WORLD_SCENE=0`: use direct scene submission while
  retaining the device's indexed material capability.
- `PAC_VULKAN_DISABLE_DESCRIPTOR_INDEXING=1`: simulate a device without the
  descriptor-indexed material path; indirect world submission is also disabled.
- `PAC_VULKAN_DISABLE_INDIRECT_WORLD=1`: keep descriptor indexing available but
  disable indirect world submission.
- `PAC_VULKAN_STATE_CACHE_LOG=1`: print sampled state-cache and indirect-path
  telemetry.

Leave all four unset for the preferred capability-driven Vulkan configuration.

## Visual Acceptance Gate

The image-diff matrix must pass both cross-backend parity and the
backend-independent expected-content guards. The guarded starter, combat, and
evolved-character regions require visible midtone surface detail and reject
excess near-black coverage, so a black-silhouette defect shared by multiple
backends cannot hide behind a small cross-backend diff. Representative captures
should still be inspected directly for art quality when framing, lighting, or
material appearance changes.
