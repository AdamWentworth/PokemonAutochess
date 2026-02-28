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

3. Release benchmark gate (manual until automated)
- Use the benchmark protocol below.
- Record results in the merge PR/notes.

## Release Benchmark Protocol (Required)

### Build
```powershell
cmake --build build --config Release --target PokemonAutochess
```

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

## Manual Parity Smoke (Before Merge)
1. Start menu -> gameplay on OpenGL and D3D12.
2. Verify board, units, combat readability, HUD, and major combat VFX.
3. Switch backend preference and restart from Display menu.
4. Confirm no crash, no missing model/material regressions, and no obvious backend-only artifacts.

## Gaps To Close Next
1. Add true GPU frame timing to perf logs.
2. Add an automated benchmark runner (`tools/benchmark_render_matrix.ps1`).
3. Add optional screenshot parity harness for a small deterministic scene set.
