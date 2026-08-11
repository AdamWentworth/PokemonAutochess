param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug",
    [switch]$IncludePreviewSmoke,
    [switch]$IncludeRuntimeVisualSmoke,
    [switch]$IncludeRenderParity,
    [switch]$IncludePerfSmoke,
    [string]$PerfConfig = "Release"
)

$ErrorActionPreference = "Stop"

$toolchain = $null
if ($env:VCPKG_ROOT) {
    $toolchain = Join-Path $env:VCPKG_ROOT "scripts/buildsystems/vcpkg.cmake"
}

if (-not $toolchain -or -not (Test-Path $toolchain)) {
    Write-Error "VCPKG_ROOT is not set or toolchain not found. Set VCPKG_ROOT and retry."
    exit 1
}

function Assert-LastExitCode {
    param(
        [string]$Step
    )

    if ($LASTEXITCODE -ne 0) {
        throw "$Step failed with exit code $LASTEXITCODE."
    }
}

$cache = Join-Path $BuildDir "CMakeCache.txt"
if (-not (Test-Path $cache)) {
    cmake -S . -B $BuildDir -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DPAC_BUILD_TOOLS=ON -DBUILD_TESTING=ON
    Assert-LastExitCode "Configure"
}

& (Join-Path $PSScriptRoot "check_docs_hygiene.ps1") -BuildDir $BuildDir

& (Join-Path $PSScriptRoot "assets\validate_kanto_gender_models.ps1")

& (Join-Path $PSScriptRoot "assets\audit_kanto_eye_handling.ps1") `
    -OutputDirectory (Join-Path $BuildDir "eye-audit")

$runPreviewSmoke = $IncludePreviewSmoke.IsPresent
if (-not $runPreviewSmoke) {
    $runPreviewSmoke = $env:PAC_ENABLE_PREVIEW_SMOKE_TESTS -eq "1"
}

$runRuntimeVisualSmoke = $IncludeRuntimeVisualSmoke.IsPresent
if (-not $runRuntimeVisualSmoke) {
    $runRuntimeVisualSmoke = $env:PAC_ENABLE_RUNTIME_VISUAL_SMOKE_TESTS -eq "1"
}

$runRenderParity = $IncludeRenderParity.IsPresent
if (-not $runRenderParity) {
    $runRenderParity = $env:PAC_ENABLE_RENDER_PARITY_TESTS -eq "1"
}

$runPerfSmoke = $IncludePerfSmoke.IsPresent
if (-not $runPerfSmoke) {
    $runPerfSmoke = $env:PAC_ENABLE_PERF_SMOKE_TESTS -eq "1"
}

if ($runPerfSmoke) {
    # Build the Release perf target before the long Debug test pass so the later
    # perf smoke measures a settled binary instead of a just-built hot run.
    cmake --build $BuildDir --config $PerfConfig --target PokemonAutochess
    Assert-LastExitCode "Perf smoke prebuild"
}

cmake --build $BuildDir --config $Config
Assert-LastExitCode "Build"
ctest --test-dir $BuildDir -C $Config --output-on-failure
Assert-LastExitCode "CTest"
cmake --build $BuildDir --config $Config --target PAC_ValidateData
Assert-LastExitCode "PAC_ValidateData"

if ($runPreviewSmoke) {
    & (Join-Path $PSScriptRoot "vfx_preview_visual_smoke.ps1") -BuildDir $BuildDir -Config $Config
    Assert-LastExitCode "Preview visual smoke"
}

if ($runRuntimeVisualSmoke) {
    & (Join-Path $PSScriptRoot "runtime_visual_smoke.ps1") -BuildDir $BuildDir -Config $Config
    Assert-LastExitCode "Runtime visual smoke"
}

if ($runRenderParity) {
    & (Join-Path $PSScriptRoot "render_parity_matrix.ps1") -BuildDir $BuildDir -Config $Config
    Assert-LastExitCode "Render parity matrix"
}

if ($runPerfSmoke) {
    & (Join-Path $PSScriptRoot "perf_smoke_guard.ps1") -BuildDir $BuildDir -Config $PerfConfig -NoBuild
    Assert-LastExitCode "Perf smoke"
}
