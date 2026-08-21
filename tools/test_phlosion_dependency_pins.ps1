param(
    [string]$GameRoot = "",
    [string]$PhlosionRoot = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $PSScriptRoot ".."
}
$GameRoot = [IO.Path]::GetFullPath($GameRoot)

if ([string]::IsNullOrWhiteSpace($PhlosionRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($env:PHLOSION_DEV_ROOT)) {
        $PhlosionRoot = $env:PHLOSION_DEV_ROOT
    } else {
        $PhlosionRoot = Join-Path $GameRoot "..\..\Phlosion"
    }
}
$PhlosionRoot = [IO.Path]::GetFullPath($PhlosionRoot)

$cmakePath = Join-Path $GameRoot "CMakeLists.txt"
$cmake = Get-Content -LiteralPath $cmakePath -Raw
$dependencies = @(
    [pscustomobject]@{
        Name = "PhlosionEngine"
        Variable = "PHLOSION_ENGINE_GIT_TAG"
    },
    [pscustomobject]@{
        Name = "PhlosionVFX"
        Variable = "PHLOSION_VFX_GIT_TAG"
    }
)

$validated = 0
$skipped = 0
foreach ($dependency in $dependencies) {
    $pattern = (
        'set\(\s*' + [regex]::Escape($dependency.Variable) +
        '\s+"([0-9a-f]{40})"')
    $match = [regex]::Match($cmake, $pattern)
    if (-not $match.Success) {
        throw "$($dependency.Variable) must contain one immutable 40-character commit SHA."
    }

    $pinned = $match.Groups[1].Value
    $checkout = Join-Path $PhlosionRoot $dependency.Name
    if (-not (Test-Path -LiteralPath (Join-Path $checkout ".git"))) {
        ++$skipped
        Write-Host "$($dependency.Name): pinned=$pinned (local checkout unavailable)"
        continue
    }

    $localHead = (& git -C $checkout rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Could not read $($dependency.Name) HEAD from $checkout."
    }
    if ($localHead -ne $pinned) {
        throw (
            "$($dependency.Name) dependency pin is stale: CMake pins $pinned " +
            "but the local workspace uses $localHead.")
    }

    ++$validated
    Write-Host "$($dependency.Name): pinned=$pinned local-head=$localHead"
}

Write-Host "Phlosion dependency pins OK: validated=$validated skipped=$skipped."
