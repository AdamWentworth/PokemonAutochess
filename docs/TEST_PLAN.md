# Test Plan

Date: 2026-02-28

Goal: catch real regressions while producing trustworthy backend parity/performance evidence.

## Automated Coverage (Current)
- Core gameplay determinism/invariants.
- Render route contracts and startup route policy.
- Video preference parsing and backend token handling.
- Backend contracts (including D3D12 probe/material constants).
- Headless and render smoke coverage (`render_pipeline_smoke`).
- Optional runtime smoke tests for `opengl` and `d3d12` when `PAC_ENABLE_RUNTIME_SMOKE_TESTS` is enabled.

## Pre-Merge Required Validation Stack
1. Debug correctness gate
- `ctest --test-dir build -C Debug --output-on-failure`

2. Runtime smoke gate (if enabled in CMake)
- `PAC_RuntimeSmoke.opengl`
- `PAC_RuntimeSmoke.d3d12`

3. Release benchmark gate
- Run the benchmark protocol below (automated runner available).
- Record results in the merge PR/notes.

## Release Benchmark Protocol (Required)

### Build
```powershell
cmake --build build --config Release --target PokemonAutochess
```

### Automated Runner (Preferred)
```powershell
.\tools\benchmark_render_matrix.ps1 -BuildDir build -Config Release -DurationSeconds 35 -Seed 12345
```
Notes:
- Builds `PokemonAutochess` for the selected config by default before running.
- Use `-NoBuild` only if you intentionally want to reuse an already-built executable.
- Scores steady-state samples by default (`-WarmupSamples 5`), skipping startup-transition noise.
- Also captures projected-path breakdown fields when available:
  - `avg_render_build_ms`, `avg_render_submit_ms`
  - `avg_projected_units_ms`, `avg_projected_pose_eval_ms`, `avg_projected_model_ms`, `avg_projected_overlay_ms`
  - `avg_projected_units_processed`, `avg_projected_model_units`

Artifacts written to `benchmark/`:
- CSV summary per matrix row.
- JSON summary + metadata.
- Raw stdout logs per row.

### Test Controls
- Keep AC power connected.
- Close heavy background apps.
- Use fixed seed for repeatability: `PAC_RANDOM_SEED=12345`.
- Use the same in-game scene/actions for every run.
- Collect at least 30 seconds per row.

### Run Matrix
Rows to capture (same scene each time):
- OpenGL at 1280x720 / 1600x900 / 1920x1080
- D3D12 at 1280x720 / 1600x900 / 1920x1080

Example launch command template:
```powershell
$env:PAC_RENDER_BACKEND='opengl'   # or d3d12
$env:PAC_RANDOM_SEED='12345'
$env:PAC_AUTO_QUIT_SECONDS='35'
.\build\Release\PokemonAutochess.exe
```

### Required Capture Fields
For each matrix row, record:
- API
- resolution
- avg FPS
- 1% low FPS (if available)
- CPU frame ms
- GPU frame ms
- present wait ms
- draw calls
- triangles
- visible animated unit count
- particle count
- render build ms
- render submit ms
- projected units ms
- projected pose eval ms
- projected model ms
- projected overlay ms
- scored sample count (`sample_count_scored`) so runs are compared on equivalent steady-state windows

### CPU-Heavy Render A/B (Diagnosis)
Use these toggles to isolate CPU-side animation/deformation cost:

```powershell
# Baseline
.\tools\benchmark_render_matrix.ps1 -BuildDir build -Config Release -DurationSeconds 35 -Seed 12345

# Disable backend per-vertex procedural deformation
.\tools\benchmark_render_matrix.ps1 -BuildDir build -Config Release -DurationSeconds 35 -Seed 12345 -BackendVertexDeform 0

# Disable clip-pose skinning (diagnostic only)
.\tools\benchmark_render_matrix.ps1 -BuildDir build -Config Release -DurationSeconds 35 -Seed 12345 -BackendClipSkinning 0
```

Interpretation:
- Large FPS gain when `-BackendVertexDeform 0` means CPU per-vertex deform math is a major hotspot.
- Large FPS gain when `-BackendClipSkinning 0` means CPU clip skinning is a major hotspot.
- If both gains are small, focus next on draw submission/state churn and expensive VFX passes.

## Manual Parity Smoke (Before Merge)
1. Start menu -> gameplay on OpenGL and D3D12.
2. Verify board, units, combat readability, HUD, and major combat VFX.
3. Switch backend preference and restart from Display menu.
4. Confirm no crash, no missing model/material regressions, and no obvious backend-only artifacts.

## Gaps To Close Next
1. Add optional screenshot parity harness for a small deterministic scene set.
2. Add CI job to run a reduced benchmark matrix on dedicated hardware.
3. Add automatic compare-against-baseline thresholds for frame-time regressions.
