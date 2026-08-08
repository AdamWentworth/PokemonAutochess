[CmdletBinding()]
param(
    [string]$GameRoot = '',
    [string]$DepotRoot = $env:PHLOSION_ASSET_DEPOT,
    [switch]$Apply
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-FullPath([string]$PathValue) {
    return [IO.Path]::GetFullPath($PathValue)
}

function Assert-PathUnderRoot {
    param([string]$PathValue, [string]$RootValue, [string]$Description)
    $path = Resolve-FullPath $PathValue
    $root = (Resolve-FullPath $RootValue).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $path.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description escapes its allowed root: $path (root: $root)"
    }
    return $path
}

function Get-RelativePathUnderRoot {
    param([string]$PathValue, [string]$RootValue)
    $path = Assert-PathUnderRoot $PathValue $RootValue 'Runtime content source'
    $root = (Resolve-FullPath $RootValue).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    return $path.Substring($root.Length)
}

function Publish-FileAtomically {
    param([string]$Source, [string]$Destination, [string]$AllowedRoot)
    $destinationPath = Assert-PathUnderRoot $Destination $AllowedRoot 'Runtime depot destination'
    New-Item -ItemType Directory -Path (Split-Path -Parent $destinationPath) -Force | Out-Null
    $nonce = [Guid]::NewGuid().ToString('N')
    $partial = "$destinationPath.partial.$nonce"
    $backup = "$destinationPath.backup.$nonce"
    Copy-Item -LiteralPath $Source -Destination $partial
    try {
        $sourceHash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
        $partialHash = (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash
        if ($sourceHash -ne $partialHash) {
            throw "Runtime depot copy failed verification: $Source"
        }
        if (Test-Path -LiteralPath $destinationPath -PathType Leaf) {
            Move-Item -LiteralPath $destinationPath -Destination $backup
        }
        Move-Item -LiteralPath $partial -Destination $destinationPath
        if (Test-Path -LiteralPath $backup -PathType Leaf) {
            Remove-Item -LiteralPath $backup -Force
        }
    } catch {
        if ((-not (Test-Path -LiteralPath $destinationPath)) -and
            (Test-Path -LiteralPath $backup -PathType Leaf)) {
            Move-Item -LiteralPath $backup -Destination $destinationPath
        }
        throw
    } finally {
        if (Test-Path -LiteralPath $partial -PathType Leaf) {
            Remove-Item -LiteralPath $partial -Force
        }
    }
}

if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $PSScriptRoot '..\..'
}
$GameRoot = Resolve-FullPath $GameRoot
if ([string]::IsNullOrWhiteSpace($DepotRoot)) {
    $DepotRoot = Join-Path $GameRoot '..\PhlosionAssets'
}
$DepotRoot = Resolve-FullPath $DepotRoot

$sourceRoot = Join-Path $GameRoot 'content\phlosion'
$destinationRoot = Join-Path $DepotRoot 'pokemon-autochess\runtime\content\phlosion'
$manifestPath = Join-Path $sourceRoot 'cook_manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Current cook manifest does not exist: $manifestPath"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ([int]$manifest.schema_version -ne 2 -or [string]$manifest.kind -ne 'phlosion_cook_manifest') {
    throw "Unsupported cook manifest: $manifestPath"
}

$sourceDirectories = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach ($entry in @($manifest.pokemon) + @($manifest.staged_imports) + @($manifest.runtime_auxiliary_objects)) {
    $objectPath = Assert-PathUnderRoot `
        (Join-Path $GameRoot ([string]$entry.object)) `
        $sourceRoot `
        'Manifest object'
    [void]$sourceDirectories.Add((Split-Path -Parent $objectPath))
}
$environmentRoot = Join-Path $sourceRoot 'objects\environment\route1'
if (-not (Test-Path -LiteralPath $environmentRoot -PathType Container)) {
    throw "Cooked Route 1 environment does not exist: $environmentRoot"
}
[void]$sourceDirectories.Add($environmentRoot)

$sourceFiles = New-Object 'System.Collections.Generic.Dictionary[string,object]' ([StringComparer]::OrdinalIgnoreCase)
foreach ($directory in $sourceDirectories) {
    foreach ($file in @(Get-ChildItem -LiteralPath $directory -Recurse -File)) {
        $sourceFiles[$file.FullName] = $file
    }
}
$scenePath = Assert-PathUnderRoot `
    (Join-Path $GameRoot ([string]$manifest.environment.scene)) `
    $sourceRoot `
    'Manifest scene'
$sourceFiles[$scenePath] = Get-Item -LiteralPath $scenePath
$sourceFiles[$manifestPath] = Get-Item -LiteralPath $manifestPath

$records = New-Object 'System.Collections.Generic.List[object]'
foreach ($file in @($sourceFiles.Values | Sort-Object FullName)) {
    $relative = Get-RelativePathUnderRoot $file.FullName $sourceRoot
    $destination = Assert-PathUnderRoot `
        (Join-Path $destinationRoot $relative) `
        $destinationRoot `
        'Runtime depot destination'
    $sourceHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $destinationHash = if (Test-Path -LiteralPath $destination -PathType Leaf) {
        (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    } else { $null }
    $records.Add([pscustomobject][ordered]@{
        source = $file.FullName
        destination = $destination
        relative_path = $relative.Replace('\', '/')
        bytes = [int64]$file.Length
        source_sha256 = $sourceHash
        destination_sha256 = $destinationHash
        requires_copy = $sourceHash -ne $destinationHash
    })
}

$changed = @($records | Where-Object requires_copy)
$changedBytes = [int64]0
foreach ($record in $changed) {
    $changedBytes += [int64]$record.bytes
}
Write-Host "Runtime depot publication plan:"
Write-Host "  source files: $($records.Count)"
Write-Host "  differing/missing files: $($changed.Count)"
Write-Host "  bytes to publish: $changedBytes"
Write-Host "  destination: $destinationRoot"
if (-not $Apply) {
    Write-Host 'Plan only; pass -Apply to publish differing files atomically.'
    return [pscustomobject][ordered]@{
        schema = 'phlosion-runtime-depot-publication-plan-v1'
        source_file_count = $records.Count
        changed_file_count = $changed.Count
        changed_bytes = $changedBytes
        destination_root = $destinationRoot
    }
}

$published = 0
$publishedBytes = [int64]0
foreach ($record in $changed) {
    $currentSourceHash = (Get-FileHash -LiteralPath $record.source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($currentSourceHash -ne $record.source_sha256) {
        throw "Runtime depot publication plan is stale: $($record.source)"
    }
    Publish-FileAtomically `
        -Source $record.source `
        -Destination $record.destination `
        -AllowedRoot $destinationRoot
    $published++
    $publishedBytes += [int64]$record.bytes
}

Write-Host "Runtime depot publication complete: $published files, $publishedBytes bytes."
return [pscustomobject][ordered]@{
    schema = 'phlosion-runtime-depot-publication-result-v1'
    published_file_count = $published
    published_bytes = $publishedBytes
    identical_file_count = $records.Count - $published
    destination_root = $destinationRoot
}
