# Test Plan

Status: Active
Type: Runbook
Last updated: 2026-03-31

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
- baseline file: `config/perf/release_perf_smoke_starter_line.json`
- scene: `config/debug/debug_state_snapshot_tail_fire_starter_line.json`
- backends: `OpenGL`, `D3D12`
- display-aware protected resolutions: `960x540`, `1280x720`, `1600x900`
- automatically selects the largest protected resolution that fits the current
  primary-display working area
- pinned scripted snapshot state during scoring to avoid shop/menu timer drift
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
- Start menu to gameplay on `OpenGL` and `D3D12`
- Verify board readability, unit rendering, HUD, and major combat VFX
- Switch backend preference and restart from the Display menu
- Confirm no missing material/model regressions and no backend-only crashes

## Optional Screenshot Parity Harness
```powershell
.\tools\render_parity_screenshot_diff.ps1 -BuildDir build -Config Debug -ScreenshotFrame 120 -AutoQuitSeconds 3
```

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
.\tools\full_check.ps1 -IncludePreviewSmoke -IncludePerfSmoke
```

## One-Command Local Check
```powershell
.\tools\full_check.ps1
```

Treat this as the convenience wrapper for the gates above, not as a separate
source of policy. If it is red because of a known repo-level issue, track that
issue in `OUTSTANDING_ISSUES.md` rather than baking transient failures into
this runbook.
