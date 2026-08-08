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
        throw "Cooked-object candidate escapes its allowed root: $path"
    }
    return $path
}

function Get-LogicalIdentity([string]$DirectoryName) {
    if ($DirectoryName -match '^(.+)-[0-9a-f]{16}$') {
        return $Matches[1]
    }
    return $null
}

if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $PSScriptRoot '..\..'
}
$GameRoot = Resolve-FullPath $GameRoot
$objectsRoot = Resolve-FullPath (Join-Path $GameRoot 'content\phlosion\objects')
$manifestPath = Join-Path $GameRoot 'content\phlosion\cook_manifest.json'
$catalogPath = Join-Path $GameRoot 'config\assets\asset_catalog.json'
if (-not (Test-Path -LiteralPath $objectsRoot -PathType Container) -or
    -not (Test-Path -LiteralPath $manifestPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $catalogPath -PathType Leaf)) {
    throw 'Cooked objects, cook manifest, and asset catalog are required.'
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$catalog = Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json
if ([int]$manifest.schema_version -ne 2 -or
    [string]$manifest.kind -ne 'phlosion_cook_manifest') {
    throw "Unsupported cook manifest: $manifestPath"
}

$ownedDirectories = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$ownedIdentities = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach ($entry in @($manifest.pokemon) + @($manifest.staged_imports) + @($manifest.runtime_auxiliary_objects)) {
    $objectPath = Assert-PathUnderRoot (Join-Path $GameRoot ([string]$entry.object)) $objectsRoot
    $directory = Split-Path -Parent $objectPath
    [void]$ownedDirectories.Add($directory)
    $logicalIdentity = Get-LogicalIdentity ([IO.Path]::GetFileName($directory))
    if (-not [string]::IsNullOrWhiteSpace($logicalIdentity)) {
        [void]$ownedIdentities.Add($logicalIdentity)
    }
}

$legacyIdentities = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach ($source in @($catalog.retained_review_sources)) {
    if ($source.PSObject.Properties.Name -contains 'legacy_cooked_identities') {
        foreach ($identity in @($source.legacy_cooked_identities)) {
            [void]$legacyIdentities.Add([string]$identity)
        }
    }
}

$candidates = @()
foreach ($directory in @(Get-ChildItem -LiteralPath $objectsRoot -Directory)) {
    $resolved = Assert-PathUnderRoot $directory.FullName $objectsRoot
    if ($directory.Name -eq 'environment' -or $ownedDirectories.Contains($resolved)) {
        continue
    }
    $logicalIdentity = Get-LogicalIdentity $directory.Name
    $classification = if ($ownedIdentities.Contains($logicalIdentity)) {
        'superseded_cooked_object'
    } elseif ($legacyIdentities.Contains($logicalIdentity)) {
        'catalogued_legacy_cooked_object'
    } else {
        $null
    }
    if ($null -eq $classification) { continue }
    $children = @(Get-ChildItem -LiteralPath $resolved -Force -Recurse)
    $reparsePoints = @($children | Where-Object {
        ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
    })
    if (($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
        $reparsePoints.Count -gt 0) {
        throw "Cooked-object candidate contains a reparse point: $resolved"
    }
    $files = @($children | Where-Object { -not $_.PSIsContainer })
    $candidates += [pscustomobject][ordered]@{
        path = $resolved
        logical_identity = $logicalIdentity
        classification = $classification
        file_count = $files.Count
        bytes = [int64](($files | Measure-Object -Property Length -Sum).Sum)
    }
}

$candidateBytes = [int64]0
foreach ($candidate in $candidates) {
    $candidateBytes += [int64]$candidate.bytes
}
$result = [pscustomobject][ordered]@{
    schema = 'phlosion-unreferenced-cooked-object-prune-v1'
    apply = [bool]$Apply
    candidate_count = $candidates.Count
    candidate_bytes = $candidateBytes
    candidates = @($candidates)
    removed_count = 0
    removed_bytes = [int64]0
}
Write-Host "Unreferenced cooked-object plan: $($result.candidate_count) directories, $($result.candidate_bytes) bytes."
foreach ($candidate in $candidates) {
    Write-Host "  $($candidate.classification): $($candidate.path) ($($candidate.bytes) bytes)"
}
if (-not $Apply) {
    Write-Host 'Plan only; pass -Apply to remove the classified generated directories.'
    return $result
}

$activeProcesses = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.ProcessName -in @('PokemonAutochess', 'PhlosionEditor', 'PhlosionForge', 'PAC_Tests')
})
if ($activeProcesses.Count -gt 0) {
    throw 'Pruning refuses to run while the game, editor, Forge, or tests are active.'
}
foreach ($candidate in $candidates) {
    $path = Assert-PathUnderRoot ([string]$candidate.path) $objectsRoot
    if ($ownedDirectories.Contains($path)) {
        throw "Cook manifest ownership changed before pruning: $path"
    }
    Remove-Item -LiteralPath $path -Recurse -Force
    if (Test-Path -LiteralPath $path) {
        throw "Cooked-object candidate still exists after pruning: $path"
    }
    $result.removed_count++
    $result.removed_bytes += [int64]$candidate.bytes
}
Write-Host "Removed $($result.removed_count) unreferenced cooked directories totaling $($result.removed_bytes) bytes."
return $result
