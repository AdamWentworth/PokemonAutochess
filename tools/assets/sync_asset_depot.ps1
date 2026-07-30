[CmdletBinding()]
param(
    [string]$DepotRoot = $env:PHLOSION_ASSET_DEPOT,
    [string]$GameRoot = "",
    [switch]$VerifyOnly
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $scriptRoot "..\.."
}
$GameRoot = [IO.Path]::GetFullPath($GameRoot)

if ([string]::IsNullOrWhiteSpace($DepotRoot)) {
    $DepotRoot = Join-Path $GameRoot "..\PhlosionAssets"
}
$DepotRoot = [IO.Path]::GetFullPath($DepotRoot)

$mappings = @(
    @{
        Source = Join-Path $DepotRoot "pokemon-autochess\runtime\assets"
        Destination = Join-Path $GameRoot "assets"
    },
    @{
        Source = Join-Path $DepotRoot "pokemon-autochess\runtime\content\phlosion"
        Destination = Join-Path $GameRoot "content\phlosion"
    }
)

foreach ($mapping in $mappings) {
    $source = [IO.Path]::GetFullPath($mapping.Source)
    $destination = [IO.Path]::GetFullPath($mapping.Destination)

    if (-not (Test-Path -LiteralPath $source -PathType Container)) {
        throw "Asset depot source does not exist: $source"
    }

    $files = Get-ChildItem -LiteralPath $source -Recurse -File
    $bytes = ($files | Measure-Object -Property Length -Sum).Sum
    Write-Host ("{0} files ({1} bytes): {2} -> {3}" -f
        $files.Count, $bytes, $source, $destination)

    if ($VerifyOnly) {
        continue
    }

    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    $children = Get-ChildItem -LiteralPath $source -Force
    foreach ($child in $children) {
        Copy-Item -LiteralPath $child.FullName -Destination $destination -Recurse -Force
    }
}

if ($VerifyOnly) {
    Write-Host "Asset depot verification passed; no files were copied."
} else {
    Write-Host "Asset depot sync complete. No destination files were deleted."
}
