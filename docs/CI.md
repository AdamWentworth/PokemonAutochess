# CI

Status: Active
Type: Runbook
Last updated: 2026-07-22

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
  - Upload runtime smoke screenshots even on failure

Optional runtime smoke tests (`PAC_ENABLE_RUNTIME_SMOKE_TESTS`):
- `PAC_RuntimeSmoke.opengl`
- `PAC_RuntimeSmoke.vulkan`
- `PAC_RuntimeSmoke.d3d12`
- `PAC_RuntimeSmoke.parity_contract` (compares OpenGL, Vulkan, and D3D12)

## What CI Does Not Yet Run
- Release benchmark matrix for renderer performance.
- Hosted-runner perf smoke gate on GitHub Actions. The current hosted Windows
  runners are too non-representative for meaningful perf thresholds.
- Deterministic screenshot/image-diff parity harness
  (`tools/render_parity_screenshot_diff.ps1`). It remains a local GPU check
  until we have a representative self-hosted runner.
- Installer end-to-end smoke.
- Merge-blocking PR perf gate.
- Merge-blocking PR visual smoke gate.
- Preview visual smoke on GitHub-hosted Windows runners; keep using it locally
  or move it to a self-hosted GPU runner later if we want CI coverage there.
- Merge-blocking PR runtime visual smoke gate.

## Required Local Supplement For Perf-Sensitive Renderer Changes
1. Run full Debug CTest locally.
2. Run the Release benchmark protocol from `docs/TEST_PLAN.md` when the change can affect frame time or startup cost.
3. Record benchmark outputs in commit notes, PR notes, or local engineering notes.

If we want automated perf gating later, the next honest step is a self-hosted
GPU runner or a benchmark environment we actually control.

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

The format check uses `clang-format` from `PATH` when available and otherwise
discovers LLVM installed by Visual Studio. Set `PAC_CLANG_FORMAT` (or pass
`-ClangFormatPath`) to select a specific executable.
