# Test Plan

Status: Active
Type: Runbook
Last updated: 2026-07-22

Goal: catch real regressions while keeping correctness, performance evidence,
preview tooling, and docs maintenance trustworthy.

## Automated Coverage
- Headless gameplay determinism and invariants
- Render route contracts and startup route policy
- Backend contracts, including D3D12 probe/material constants
- Renderer parity contract checks
- Tail Fire and Growl shared-path contract coverage
- Optional runtime smoke tests when `PAC_ENABLE_RUNTIME_SMOKE_TESTS` is enabled
- Docs hygiene validation via `tools/check_docs_hygiene.ps1`

## Required Local Validation
1. Debug correctness gate

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

2. Docs hygiene gate

```powershell
.\tools\check_docs_hygiene.ps1
```

3. Developer-tool build gate

```powershell
cmake --build build --config Debug --target PAC_VfxPreviewer VfxLab
```

4. Data validation gate

```powershell
cmake --build build --config Debug --target PAC_ValidateData
```

5. Release benchmark protocol for perf-sensitive render/runtime changes

```powershell
.\tools\benchmark_render_matrix.ps1 -BuildDir build -Config Release -DurationSeconds 35 -Seed 12345
```

Use `-VideoCharacterInking 0|1` to pin the outline workload. Do not compare a
run with inking disabled against one with it enabled.

Discard any benchmark artifact with `sample_count == 0` or
`sample_count_scored < min_scored_samples`; those are invalid evidence, not a
baseline.

## Optional Release Perf Smoke
Use this when you want a fast protected baseline check on a stable local runner
without paying for the full benchmark matrix.

```powershell
.\tools\perf_smoke_guard.ps1 -BuildDir build -Config Release
```

This guard currently uses:
- baseline suite:
  - `config/perf/release_perf_smoke_starter_line.json`
  - `config/perf/release_perf_smoke_dense_roster.json`
- scenes:
  - `config/debug/debug_state_snapshot_tail_fire_starter_line.json`
  - `config/debug/debug_state_snapshot_perf_dense_roster.json`
- backends: `OpenGL`, `D3D12`
- each baseline auto-selects the largest protected resolution that fits the
  current primary-display working area
- scripted snapshot state is pinned during scoring to avoid shop/menu timer
  drift
- `full_check.ps1 -IncludePerfSmoke` prebuilds the Release target before the
  long Debug gate and then runs the smoke with `-NoBuild`, so the scored run is
  not measuring a just-built hot binary
- if no protected perf-smoke resolution fits the current display, the harness
  fails clearly instead of launching a stretched or overflowing run

If the Release binary is already current, prefer:

```powershell
.\tools\perf_smoke_guard.ps1 -BuildDir build -Config Release -NoBuild
```

If you want to inspect the same scene manually at a specific local window size
instead of using the protected dynamic baseline, run a non-baselined probe:

```powershell
.\tools\benchmark_render_matrix.ps1 -BuildDir build -Config Release -NoBuild -Backends opengl,d3d12 -Resolutions 1280x720 -DurationSeconds 12 -WarmupSamples 3 -MinScoredSamples 6 -SnapshotPath config/debug/debug_state_snapshot_tail_fire_starter_line.json -AutoLoadSnapshot -PinSnapshotState -VideoVsync 0 -VideoFpsCap 0 -Tag local_probe
```

It is intentionally lighter than the full benchmark matrix and should be read
as a smoke-level regression guard, not the final word on renderer performance.

## VFX And Preview Validation
Run these checks when work touches shared VFX, preview adapters, authored
Tail Fire playback, or preview tooling.

### Build Targets
```powershell
cmake --build build --config Debug --target PAC_VfxPreviewer VfxLab PAC_Tests
```

### Contract Tests
```powershell
.\build\Debug\PAC_Tests.exe `
  --filter shared_tail_fire_mesh_playback_contract `
  --filter shared_tail_fire_playback_policy_contract `
  --filter shared_growl_vfx_helpers_contract `
  --filter shared_growl_wave_bridge_contract `
  --filter shared_growl_wave_batches_contract `
  --filter runtime_growl_vfx_prewarm_contract `
  --filter runtime_particle_vfx_prewarm_contract
```

### Manual Tool Smoke
- `PAC_VfxPreviewer`
  - confirm Growl hot reload still works
  - confirm Charmander Tail Fire appears via authored playback when available
  - confirm synthetic Tail Fire fallback still appears when authored playback
    does not resolve
- `VfxLab`
  - confirm Growl replay/reload/step behavior still works
- Leech Seed preview
  - expected behavior today is projectile-only; do not treat missing drain
    behavior in the preview tool as a regression until the adapter grows

## Runtime Parity Smoke
- Start menu to gameplay on `OpenGL`, `Vulkan`, and `D3D12`
- Verify board readability, unit rendering, HUD, and major combat VFX
- Switch backend preference and restart from the Display menu
- Confirm no missing material/model regressions and no backend-only crashes

## Optional Runtime Visual Smoke
Use this when work touches gameplay presentation, HUD/layout, unit rendering, or
backend-specific runtime visuals and you want an automated screenshot sanity
check instead of only manual launches.

```powershell
.\tools\runtime_visual_smoke.ps1 -BuildDir build -Config Debug
```

This harness currently:
- loads the Tail Fire starter-line snapshot
- pins the scripted snapshot state during capture
- auto-selects the largest supported smoke resolution that fits the current
  display from `960x540`, `1280x720`
- captures deterministic runtime screenshots on `OpenGL`, `Vulkan`, and `D3D12`
- checks coarse HUD and gameplay board regions for plausible brightness/color
  content instead of relying on pixel-perfect parity

Optional one-command local check:

```powershell
.\tools\full_check.ps1 -IncludeRuntimeVisualSmoke
```

Or enable it through the environment:

```powershell
$env:PAC_ENABLE_RUNTIME_VISUAL_SMOKE_TESTS = "1"
.\tools\full_check.ps1
```

## Optional Screenshot Parity Harness
```powershell
.\tools\render_parity_matrix.ps1 -BuildDir build -Config Debug
```

This is the stricter renderer-parity check. Its manifest-driven scene matrix
covers static PBR/environment rendering, transparent tail-fire VFX, Route 1
combat presentation, and the startup UI. Every capture uses the same fixed
frame delta and random seed across `OpenGL`, `Vulkan`, and `D3D12`, then compares
each backend to the OpenGL reference without relaxing the established image
thresholds.

When character inking or material submission changes, force inking on and
inspect the source captures as well as the numeric report:

```powershell
$env:PAC_VIDEO_CHARACTER_INKING = "1"
.\tools\render_parity_matrix.ps1 -BuildDir build -Config Release
Remove-Item Env:PAC_VIDEO_CHARACTER_INKING
```

Verify that starter, combat, and evolved models remain colored and textured.
A black-silhouette bug shared by multiple backends can still produce a small
cross-backend image difference, so numeric parity alone is insufficient here.

Exercise Vulkan's direct compatibility path with:

```powershell
$env:PAC_VIDEO_CHARACTER_INKING = "1"
$env:PAC_VULKAN_DISABLE_DESCRIPTOR_INDEXING = "1"
.\tools\render_parity_matrix.ps1 `
  -BuildDir build -Config Release `
  -Cases static-pbr -Backends opengl,vulkan -ReferenceBackend opengl
Remove-Item Env:PAC_VIDEO_CHARACTER_INKING
Remove-Item Env:PAC_VULKAN_DISABLE_DESCRIPTOR_INDEXING
```

Each scene directory contains source PNGs, amplified heatmaps, backend logs,
and a machine-readable `report.json` with normalized MAE, RMSE,
maximum-channel error, and changed-pixel ratio. The matrix root also contains
`matrix-report.json`, which records coverage metadata and the pass/fail result
for every scene. Scene definitions and shared capture limits live in
`config/render_parity_scene_matrix.json`.

List or select focused cases with:

```powershell
.\tools\render_parity_matrix.ps1 -ListCases
.\tools\render_parity_matrix.ps1 -Cases combat,ui
```

For an ad-hoc single snapshot comparison, use the atomic scene runner directly:

```powershell
.\tools\render_parity_screenshot_diff.ps1 -BuildDir build -Config Debug
```

Use `-ReportOnly` while investigating a known difference. Use `-SkipCapture` to
recompute metrics and heatmaps from an existing output directory without
launching the game again.

## Optional Preview Visual Smoke
Use this when work touches preview-tool visuals, Growl preview parity, or the
Pokemon-model presentation path inside `PAC_VfxPreviewer`.

```powershell
.\tools\vfx_preview_visual_smoke.ps1 -BuildDir build -Config Debug
```

This harness currently checks:
- `VfxLab` Growl color/content output in a deterministic center crop.
- `PAC_VfxPreviewer` 3D-model Growl/model presentation output in a deterministic
  model crop.

Optional one-command local check:

```powershell
.\tools\full_check.ps1 -IncludePreviewSmoke
```

Or enable it through the environment:

```powershell
$env:PAC_ENABLE_PREVIEW_SMOKE_TESTS = "1"
.\tools\full_check.ps1
```

Optional combined local check:

```powershell
.\tools\full_check.ps1 -IncludePreviewSmoke -IncludeRuntimeVisualSmoke -IncludePerfSmoke
```

## One-Command Local Check
```powershell
.\tools\full_check.ps1
```

Treat this as the convenience wrapper for the gates above, not as a separate
source of policy. If it is red because of a known repo-level issue, track that
issue in `OUTSTANDING_ISSUES.md` rather than baking transient failures into
this runbook.
