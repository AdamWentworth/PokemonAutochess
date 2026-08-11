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
        throw "Native-import candidate escapes its allowed root: $path"
    }
    return $path
}

function Normalize-ProjectPath([string]$PathValue) {
    return $PathValue.Replace('\', '/').TrimStart('./')
}

if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $PSScriptRoot '..\..'
}
$GameRoot = Resolve-FullPath $GameRoot
$modelsRoot = Resolve-FullPath (Join-Path $GameRoot 'assets\models')
$catalogPath = Join-Path $GameRoot 'config\assets\asset_catalog.json'
if (-not (Test-Path -LiteralPath $modelsRoot -PathType Container) -or
    -not (Test-Path -LiteralPath $catalogPath -PathType Leaf)) {
    throw 'The native model directory and asset catalog are required.'
}

$catalog = Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json
if ([string]$catalog.kind -ne 'pokemon_autochess_asset_catalog' -or
    [int]$catalog.schema_version -ne 1) {
    throw "Unsupported asset catalog: $catalogPath"
}

$ownedStems = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach ($set in @($catalog.native_import_sets)) {
    $recipePath = Assert-PathUnderRoot (Join-Path $GameRoot (Normalize-ProjectPath ([string]$set.recipe))) $GameRoot
    $recipe = Get-Content -LiteralPath $recipePath -Raw | ConvertFrom-Json
    $includedStems = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    if ([string]$set.selection -eq 'include_stems') {
        foreach ($stem in @($set.stems)) { [void]$includedStems.Add([string]$stem) }
    } elseif ([string]$set.selection -ne 'all_outputs') {
        throw "Unsupported native import selection: $($set.selection)"
    }
    foreach ($import in @($recipe.imports)) {
        foreach ($output in @($import.outputs)) {
            $stem = [string]$output.stem
            if ([string]$set.selection -eq 'all_outputs' -or $includedStems.Contains($stem)) {
                [void]$ownedStems.Add($stem)
            }
        }
    }
}
foreach ($model in @($catalog.explicit_native_models)) {
    [void]$ownedStems.Add([string]$model.stem)
}

$canonicalPattern = '^\d{4}_.+_(?:SV|LGPE|PLA|Sword|ZA)(?:_Female)?(?:_Shiny)?$'
$candidateStems = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach ($file in @(Get-ChildItem -LiteralPath $modelsRoot -File)) {
    $stem = $null
    if ($file.Name.EndsWith('.animset.json', [StringComparison]::OrdinalIgnoreCase)) {
        $stem = $file.Name.Substring(0, $file.Name.Length - '.animset.json'.Length)
    } elseif ($file.Extension -eq '.phmodel') {
        $stem = $file.BaseName
    }
    if ($null -ne $stem -and $stem -match $canonicalPattern -and -not $ownedStems.Contains($stem)) {
        [void]$candidateStems.Add($stem)
    }
}
foreach ($directory in @(Get-ChildItem -LiteralPath $modelsRoot -Directory)) {
    if ($directory.Name.EndsWith('_textures', [StringComparison]::OrdinalIgnoreCase)) {
        $stem = $directory.Name.Substring(0, $directory.Name.Length - '_textures'.Length)
        if ($stem -match $canonicalPattern -and -not $ownedStems.Contains($stem)) {
            [void]$candidateStems.Add($stem)
        }
    }
}

$candidates = @()
foreach ($stem in @($candidateStems | Sort-Object)) {
    $paths = @(
        (Join-Path $modelsRoot "$stem.phmodel"),
        (Join-Path $modelsRoot "$stem.animset.json"),
        (Join-Path $modelsRoot "${stem}_textures")
    ) | Where-Object { Test-Path -LiteralPath $_ }
    if ($paths.Count -eq 0) { continue }
    $bytes = [int64]0
    $fileCount = 0
    foreach ($candidatePath in $paths) {
        $resolved = Assert-PathUnderRoot $candidatePath $modelsRoot
        $item = Get-Item -LiteralPath $resolved -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Native-import candidate is a reparse point: $resolved"
        }
        if ($item.PSIsContainer) {
            $children = @(Get-ChildItem -LiteralPath $resolved -Force -Recurse)
            if (@($children | Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 }).Count -gt 0) {
                throw "Native-import candidate contains a reparse point: $resolved"
            }
            $files = @($children | Where-Object { -not $_.PSIsContainer })
            $fileCount += $files.Count
            $bytes += [int64](($files | Measure-Object -Property Length -Sum).Sum)
        } else {
            $fileCount++
            $bytes += [int64]$item.Length
        }
    }
    $candidates += [pscustomobject][ordered]@{
        stem = $stem
        paths = @($paths | ForEach-Object { Resolve-FullPath $_ })
        file_count = $fileCount
        bytes = $bytes
    }
}

$candidateBytes = [int64]0
foreach ($candidate in $candidates) {
    $candidateBytes += [int64]$candidate.bytes
}
$result = [pscustomobject][ordered]@{
    schema = 'phlosion-unreferenced-native-import-prune-v1'
    apply = [bool]$Apply
    candidate_count = $candidates.Count
    candidate_bytes = $candidateBytes
    candidates = @($candidates)
    removed_count = 0
    removed_bytes = [int64]0
}
Write-Host "Unreferenced native-import plan: $($result.candidate_count) stems, $($result.candidate_bytes) bytes."
foreach ($candidate in $candidates) {
    Write-Host "  $($candidate.stem): $($candidate.file_count) files, $($candidate.bytes) bytes"
}
if (-not $Apply) {
    Write-Host 'Plan only; pass -Apply to remove only the classified generated files and texture directories.'
    return $result
}

$activeWriters = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.ProcessName -in @('PhlosionForge', 'PAC_Tests')
})
if ($activeWriters.Count -gt 0) {
    throw 'Pruning refuses to run while Forge or tests may be writing native imports.'
}
foreach ($candidate in $candidates) {
    if ($ownedStems.Contains([string]$candidate.stem)) {
        throw "Asset-catalog ownership changed before pruning: $($candidate.stem)"
    }
    foreach ($candidatePath in @($candidate.paths)) {
        $resolved = Assert-PathUnderRoot ([string]$candidatePath) $modelsRoot
        Remove-Item -LiteralPath $resolved -Recurse -Force
        if (Test-Path -LiteralPath $resolved) {
            throw "Native-import candidate still exists after pruning: $resolved"
        }
    }
    $result.removed_count++
    $result.removed_bytes += [int64]$candidate.bytes
}
Write-Host "Removed $($result.removed_count) unreferenced native-import stems totaling $($result.removed_bytes) bytes."
return $result
