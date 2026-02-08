param(
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [string]$OutDir = "dist/Release",
    [switch]$IncludeVcpkgDlls,
    [switch]$Clean
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

cmake --build $BuildDir --config $Config --target PokemonAutochess
cmake --build $BuildDir --config $Config --target PAC_ValidateData
cmake --build $BuildDir --config $Config --target PAC_PackData

if ($Clean -and (Test-Path $OutDir)) {
    Remove-Item $OutDir -Recurse -Force
}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$exeCandidates = @(
    (Join-Path $BuildDir "$Config\\PokemonAutochess.exe"),
    (Join-Path $BuildDir "PokemonAutochess.exe")
)
$exePath = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exePath) {
    Write-Error "PokemonAutochess.exe not found under $BuildDir (config $Config)."
    exit 1
}
Copy-Item $exePath -Destination $OutDir -Force

$exeDir = Split-Path $exePath -Parent
Get-ChildItem $exeDir -Filter *.dll -ErrorAction SilentlyContinue |
    ForEach-Object { Copy-Item $_.FullName -Destination $OutDir -Force }

$packPath = Join-Path (Get-Location) "content_pak\\content.pak"
if (Test-Path $packPath) {
    $packDir = Join-Path $OutDir "content_pak"
    New-Item -ItemType Directory -Path $packDir -Force | Out-Null
    Copy-Item $packPath -Destination $packDir -Force
} else {
    Write-Warning "content_pak/content.pak not found (PAC_PackData may have failed)."
}

$assetsPath = Join-Path (Get-Location) "assets"
if (Test-Path $assetsPath) {
    Copy-Item $assetsPath -Destination (Join-Path $OutDir "assets") -Recurse -Force
} else {
    Write-Warning "assets/ not found."
}

$configPath = Join-Path (Get-Location) "config"
if (Test-Path $configPath) {
    Copy-Item $configPath -Destination (Join-Path $OutDir "config") -Recurse -Force
} else {
    Write-Warning "config/ not found."
}

$scriptsPath = Join-Path (Get-Location) "scripts"
if (Test-Path $scriptsPath) {
    Copy-Item $scriptsPath -Destination (Join-Path $OutDir "scripts") -Recurse -Force
} else {
    Write-Warning "scripts/ not found."
}

if ($IncludeVcpkgDlls) {
    $vcpkgBin = Join-Path $BuildDir "vcpkg_installed\\x64-windows\\bin"
    if (Test-Path $vcpkgBin) {
        Get-ChildItem $vcpkgBin -Filter *.dll -ErrorAction SilentlyContinue |
            ForEach-Object { Copy-Item $_.FullName -Destination $OutDir -Force }
    } else {
        Write-Warning "vcpkg bin not found at $vcpkgBin."
    }
}

Write-Host "Release bundle created at $OutDir"
