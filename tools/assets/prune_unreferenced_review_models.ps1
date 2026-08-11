[CmdletBinding()]
param(
    [string]$GameRoot = '',
    [switch]$Apply
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-FullPath([string]$PathValue) {
    return [IO.Path]::GetFullPath($PathValue).TrimEnd('\', '/')
}

function Assert-PathUnderRoot([string]$PathValue, [string]$RootValue) {
    $path = Resolve-FullPath $PathValue
    $root = (Resolve-FullPath $RootValue) + [IO.Path]::DirectorySeparatorChar
    if (-not $path.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Review-model candidate escapes its allowed root: $path"
    }
    return $path
}

function Normalize-ProjectPath([string]$PathValue) {
    return $PathValue.Replace('\', '/').TrimStart('./')
}

function Get-ProjectRelativePath([string]$PathValue, [string]$RootValue) {
    $path = Assert-PathUnderRoot $PathValue $RootValue
    $root = Resolve-FullPath $RootValue
    return $path.Substring($root.Length + 1).Replace('\', '/')
}

if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $PSScriptRoot '..\..'
}
$GameRoot = Resolve-FullPath $GameRoot
$modelsRoot = Resolve-FullPath (Join-Path $GameRoot 'assets\models')
$catalogPath = Join-Path $GameRoot 'config\assets\asset_catalog.json'
if (-not (Test-Path -LiteralPath $modelsRoot -PathType Container) -or
    -not (Test-Path -LiteralPath $catalogPath -PathType Leaf)) {
    throw 'The model directory and asset catalog are required.'
}

$catalog = Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json
if ([string]$catalog.kind -ne 'pokemon_autochess_asset_catalog' -or
    [int]$catalog.schema_version -ne 1) {
    throw "Unsupported asset catalog: $catalogPath"
}

$ownedPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach ($source in @($catalog.authored_runtime_sources) + @($catalog.retained_review_sources)) {
    if ($source.PSObject.Properties.Name -contains 'source') {
        [void]$ownedPaths.Add((Normalize-ProjectPath ([string]$source.source)))
    }
    if ($source.PSObject.Properties.Name -contains 'animset') {
        [void]$ownedPaths.Add((Normalize-ProjectPath ([string]$source.animset)))
    }
}

$candidates = @()
foreach ($file in @(Get-ChildItem -LiteralPath $modelsRoot -File -Filter '*.glb' | Sort-Object Name)) {
    if ($file.BaseName -notmatch '^\d{4}_.+$') { continue }
    $glbRelative = Get-ProjectRelativePath $file.FullName $GameRoot
    if ($ownedPaths.Contains($glbRelative)) { continue }
    $paths = @($file.FullName)
    $animsetPath = Join-Path $modelsRoot "$($file.BaseName).animset.json"
    $animsetRelative = Get-ProjectRelativePath $animsetPath $GameRoot
    if (Test-Path -LiteralPath $animsetPath -PathType Leaf) {
        if ($ownedPaths.Contains($animsetRelative)) {
            throw "Unowned GLB has a separately owned animation set: $glbRelative"
        }
        $paths += $animsetPath
    }
    $bytes = [int64]0
    foreach ($candidatePath in $paths) {
        $resolved = Assert-PathUnderRoot $candidatePath $modelsRoot
        $item = Get-Item -LiteralPath $resolved -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Review-model candidate is a reparse point: $resolved"
        }
        $bytes += [int64]$item.Length
    }
    $candidates += [pscustomobject][ordered]@{
        stem = $file.BaseName
        paths = @($paths | ForEach-Object { Resolve-FullPath $_ })
        file_count = $paths.Count
        bytes = $bytes
    }
}

$candidateBytes = [int64]0
foreach ($candidate in $candidates) { $candidateBytes += [int64]$candidate.bytes }
$result = [pscustomobject][ordered]@{
    schema = 'phlosion-unreferenced-review-model-prune-v1'
    apply = [bool]$Apply
    candidate_count = $candidates.Count
    candidate_bytes = $candidateBytes
    candidates = @($candidates)
    removed_count = 0
    removed_bytes = [int64]0
}
Write-Host "Unreferenced review-model plan: $($result.candidate_count) stems, $($result.candidate_bytes) bytes."
foreach ($candidate in $candidates) {
    Write-Host "  $($candidate.stem): $($candidate.file_count) files, $($candidate.bytes) bytes"
}
if (-not $Apply) {
    Write-Host 'Plan only; pass -Apply to remove only the classified GLB/animation-set pairs.'
    return $result
}

$activeWriters = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.ProcessName -in @('PhlosionForge', 'PAC_Tests')
})
if ($activeWriters.Count -gt 0) {
    throw 'Pruning refuses to run while Forge or tests may be writing model assets.'
}
foreach ($candidate in $candidates) {
    foreach ($candidatePath in @($candidate.paths)) {
        $resolved = Assert-PathUnderRoot ([string]$candidatePath) $modelsRoot
        $relative = Get-ProjectRelativePath $resolved $GameRoot
        if ($ownedPaths.Contains($relative)) {
            throw "Asset-catalog ownership changed before pruning: $relative"
        }
        Remove-Item -LiteralPath $resolved -Force
        if (Test-Path -LiteralPath $resolved) {
            throw "Review-model candidate still exists after pruning: $resolved"
        }
    }
    $result.removed_count++
    $result.removed_bytes += [int64]$candidate.bytes
}
Write-Host "Removed $($result.removed_count) unreferenced review-model stems totaling $($result.removed_bytes) bytes."
return $result
