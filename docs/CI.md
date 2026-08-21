# CI

Status: Active
Type: Runbook
Last updated: 2026-08-20

CI is correctness-first and Windows-first.

## What CI Runs
- Core Windows PR/push gate:
  - Configure the standalone game/tool/test graph with the vcpkg toolchain;
    editor integration stays in the local paired-build gate because the
    first-party `PhlosionPackages` workspace is not a public CI dependency.
  - Build Debug.
  - Run CTest.
    Each C++ contract has a two-minute process timeout so a Windows crash or
    deadlock is reported as the responsible test instead of consuming the
    entire job timeout. The full-corpus model parser has an explicit ten-minute
    allowance because its local qualification pass is intentionally heavier.
    The hosted wrapper publishes entries from `LastTestsFailed.log` as job
    annotations and a job-summary list, so runner-only failures remain visible
    through the Checks API even when raw Actions logs require authentication.
  - Run `tools/check_docs_hygiene.ps1`.
  - Run `PAC_ValidateData`.
  - Run clang-format check on changed C++ files.
  - Parse every tracked PowerShell module/script and compile every tracked
    Python tool before the expensive configure/build stages.
- Dedicated Windows smoke lanes on `workflow_dispatch` and nightly `schedule`:
  - Hosted-runner runtime visual smoke: `tools/runtime_visual_smoke.ps1`
    using the `D3D12` path. The runner exits two frames after its requested
    screenshot instead of rendering hundreds of unnecessary software-D3D12
    frames against a wall-clock timeout.
  - Upload runtime smoke screenshots even on failure

Official Actions are pinned to immutable release commits and updated through
weekly Dependabot checks. The vcpkg executable checkout is pinned to the same
commit as `vcpkg.json`'s builtin baseline, so a fresh upstream vcpkg commit
cannot change CI behavior between otherwise identical runs. Its registry clone
retains history because manifest overrides, such as Lua 5.4.8, need older port
trees even though the vcpkg executable itself is pinned.

`PAC_Tools.phlosion_dependency_pins` compares the Engine and VFX commits in
`CMakeLists.txt` with local sibling checkout HEADs whenever that workspace is
available. This prevents local builds from silently validating newer sibling
APIs while clean-clone CI still fetches stale dependency pins.

The private model/source corpus under `assets/` and cooked runtime corpus under
`content/phlosion/` are intentionally not stored in GitHub. CMake labels their
26 exclusive qualification checks `private-assets` and registers them only
when representative source, mesh, shader, and cooked-scene markers are present.
Clean hosted checkouts therefore run every asset-independent contract instead
of failing tests they cannot satisfy; complete development workspaces retain
the full 261-test gate. Run `ctest --test-dir build -C Debug -L private-assets`
to select the local corpus qualification partition explicitly.

Optional runtime smoke tests (`PAC_ENABLE_RUNTIME_SMOKE_TESTS`):
- `PAC_RuntimeSmoke.opengl`
- `PAC_RuntimeSmoke.vulkan`
- `PAC_RuntimeSmoke.d3d12`
- `PAC_RuntimeSmoke.parity_contract` (compares OpenGL, Vulkan, and D3D12)

## What CI Does Not Yet Run
- Release benchmark matrix for renderer performance.
- Hosted-runner perf smoke gate on GitHub Actions. The current hosted Windows
  runners are too non-representative for meaningful perf thresholds.
- Deterministic screenshot/image-diff parity matrix
  (`tools/render_parity_matrix.ps1`, backed by the atomic
  `tools/render_parity_screenshot_diff.ps1` runner). It remains a local GPU
  check until we have a representative self-hosted runner. The aggregate
  `tools/renderer_qualification.ps1` command packages this matrix with backend
  contract, Vulkan direct-fallback, and adapter/driver evidence, but is likewise
  local-only today.
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
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DPAC_BUILD_TOOLS=ON -DPAC_BUILD_EDITOR=OFF -DBUILD_TESTING=ON
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
