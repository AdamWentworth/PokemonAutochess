# CI

Status: Active
Type: Runbook
Last updated: 2026-04-01

CI is correctness-first and Windows-first.

## What CI Runs
- Core Windows PR/push gate:
  - Configure with vcpkg toolchain.
  - Build Debug.
  - Run CTest.
  - Run `tools/check_docs_hygiene.ps1`.
  - Run `PAC_ValidateData`.
  - Run clang-format check on changed C++ files.
- Dedicated Windows smoke lanes on `workflow_dispatch` and nightly `schedule`:
  - Hosted-runner runtime visual smoke: `tools/runtime_visual_smoke.ps1`
    using the `D3D12` path
  - Hosted-runner conservative Release perf smoke slice from
    `tools/perf_smoke_guard.ps1` using the dense-roster baseline and `D3D12`
  - Upload screenshot / benchmark artifacts even on failure

Optional runtime smoke tests (`PAC_ENABLE_RUNTIME_SMOKE_TESTS`):
- `PAC_RuntimeSmoke.opengl`
- `PAC_RuntimeSmoke.d3d12`
- `PAC_RuntimeSmoke.parity_contract`

## What CI Does Not Yet Run
- Release benchmark matrix for renderer performance.
- Screenshot/image-diff parity harness (`tools/render_parity_screenshot_diff.ps1`).
- Installer end-to-end smoke.
- Merge-blocking PR perf gate.
- Merge-blocking PR visual smoke gate.
- Preview visual smoke on GitHub-hosted Windows runners; keep using it locally
  or move it to a self-hosted GPU runner later if we want CI coverage there.

## Required Local Supplement For Perf-Sensitive Renderer Changes
1. Run full Debug CTest locally.
2. Run the Release benchmark protocol from `docs/TEST_PLAN.md` when the change can affect frame time or startup cost.
3. Record benchmark outputs in commit notes, PR notes, or local engineering notes.

## Local Equivalent
```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DPAC_BUILD_TOOLS=ON -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Debug --target PAC_ValidateData
```

One-command local check:
```powershell
.\tools\full_check.ps1
```

Format check (changed files):
```powershell
.\tools\format_check.ps1
```
