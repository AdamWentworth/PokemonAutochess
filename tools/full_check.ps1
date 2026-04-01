param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug",
    [switch]$IncludePreviewSmoke
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

& (Join-Path $PSScriptRoot "check_docs_hygiene.ps1")
cmake --build $BuildDir --config $Config
Assert-LastExitCode "Build"
ctest --test-dir $BuildDir -C $Config --output-on-failure
Assert-LastExitCode "CTest"
cmake --build $BuildDir --config $Config --target PAC_ValidateData
Assert-LastExitCode "PAC_ValidateData"

$runPreviewSmoke = $IncludePreviewSmoke.IsPresent
if (-not $runPreviewSmoke) {
    $runPreviewSmoke = $env:PAC_ENABLE_PREVIEW_SMOKE_TESTS -eq "1"
}

if ($runPreviewSmoke) {
    & (Join-Path $PSScriptRoot "vfx_preview_visual_smoke.ps1") -BuildDir $BuildDir -Config $Config
    Assert-LastExitCode "Preview visual smoke"
}
