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

## One-Command Local Check
```powershell
.\tools\full_check.ps1
```

Treat this as the convenience wrapper for the gates above, not as a separate
source of policy. If it is red because of a known repo-level issue, track that
issue in `OUTSTANDING_ISSUES.md` rather than baking transient failures into
this runbook.
