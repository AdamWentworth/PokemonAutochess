# CI

CI is Windows-first and mirrors the local flow: configure, build, test, validate data.

## What Runs
- Configure with the vcpkg toolchain.
- Build Debug.
- Run CTest.
- CTest includes route-boundary contracts (render route ownership, frame/UI route policy, and backend-vs-legacy world model-attachment behavior).
- Run `PAC_ValidateData`.
- Use a vcpkg binary cache for faster rebuilds.
- Run a clang-format check on changed C++ files.
- Release installer build is not part of CI (run locally with `tools/build_installer.ps1 -Bundle`).

## Why It Exists
- Catch regressions before they land.
- Keep a clean signal for portfolio reviewers.

## Local Equivalent
```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DPAC_BUILD_TOOLS=ON -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Debug --target PAC_ValidateData
```

One-command local check
```powershell
.\tools\full_check.ps1
```

Format check (changed files only)
```powershell
.\tools\format_check.ps1
```
