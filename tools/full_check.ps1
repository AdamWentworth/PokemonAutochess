param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug"
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

$cache = Join-Path $BuildDir "CMakeCache.txt"
if (-not (Test-Path $cache)) {
    cmake -S . -B $BuildDir -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DPAC_BUILD_TOOLS=ON -DBUILD_TESTING=ON
}

cmake --build $BuildDir --config $Config
ctest --test-dir $BuildDir -C $Config --output-on-failure
cmake --build $BuildDir --config $Config --target PAC_ValidateData
