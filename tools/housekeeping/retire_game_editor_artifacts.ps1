[CmdletBinding()]
param(
    [string]$GameRoot = '',
    [switch]$Execute,
    [switch]$ConfirmDeletion
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $scriptRoot '..\..'
}
$GameRoot = [IO.Path]::GetFullPath($GameRoot)
$buildRoot = [IO.Path]::GetFullPath(
    (Join-Path $GameRoot 'build')).TrimEnd('\', '/')
$cachePath = Join-Path $buildRoot 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    throw "Active game CMake cache is missing: $cachePath"
}
$cacheSetting = Select-String `
    -LiteralPath $cachePath `
    -Pattern '^PHLOSION_BUILD_EDITOR:BOOL=' |
    Select-Object -First 1
if (-not $cacheSetting -or
    $cacheSetting.Line -ne 'PHLOSION_BUILD_EDITOR:BOOL=OFF') {
    throw 'Active game build is not proven editor-disabled.'
}

$relativeTargets = @(
    'Debug/PhlosionEditor.exe',
    'Debug/PhlosionEditor.pdb',
    'Release/PhlosionEditor.exe',
    'Release/PhlosionEditor.pdb',
    'PhlosionEditor.dir',
    'phlosion-engine/PhlosionEditor.dir',
    'phlosion-engine/PhlosionEditor.vcxproj',
    'phlosion-engine/PhlosionEditor.vcxproj.filters',
    'phlosion-engine/PhlosionEditor.vcxproj.user'
)
$targets = @($relativeTargets | ForEach-Object {
    $relativePath = $_
    $fullPath = [IO.Path]::GetFullPath(
        (Join-Path $buildRoot (
            $relativePath.Replace(
                '/',
                [IO.Path]::DirectorySeparatorChar))))
    $prefix = $buildRoot + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith(
            $prefix,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Editor artifact escaped the active build root: $fullPath"
    }
    if (Test-Path -LiteralPath $fullPath) {
        $item = Get-Item -LiteralPath $fullPath -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Editor artifact is a reparse point: $fullPath"
        }
        $bytes = if ($item.PSIsContainer) {
            [int64]((Get-ChildItem -LiteralPath $fullPath -Recurse -Force -File |
                Measure-Object Length -Sum).Sum)
        } else {
            [int64]$item.Length
        }
        [pscustomobject][ordered]@{
            path = $fullPath
            relative_path = $relativePath
            kind = if ($item.PSIsContainer) { 'directory' } else { 'file' }
            bytes = $bytes
        }
    }
})

$totalBytes = if ($targets.Count -gt 0) {
    [int64](($targets.bytes | Measure-Object -Sum).Sum)
} else {
    [int64]0
}
$mode = if ($Execute) { 'execute' } else { 'plan-only' }
Write-Host "Game-local editor retirement ($mode)"
Write-Host "Active cache: PHLOSION_BUILD_EDITOR=OFF"
foreach ($target in $targets) {
    Write-Host "- $($target.relative_path) [$($target.kind), $($target.bytes) bytes]"
}
Write-Host "Targets: $($targets.Count); bytes: $totalBytes"

if ($Execute) {
    if (-not $ConfirmDeletion) {
        throw 'Actual removal requires both -Execute and -ConfirmDeletion.'
    }
    if (@(Get-Process -Name PhlosionEditor -ErrorAction SilentlyContinue).Count -gt 0) {
        throw 'PhlosionEditor is running; refusing stale-artifact removal.'
    }
    foreach ($target in $targets) {
        # The paths above come exclusively from the fixed allowlist and have
        # already passed build-root containment and reparse-point checks.
        Remove-Item -LiteralPath $target.path -Recurse -Force
    }
    $survivors = @($targets | Where-Object {
        Test-Path -LiteralPath $_.path
    })
    if ($survivors.Count -gt 0) {
        throw "Failed to remove: $($survivors.relative_path -join ', ')"
    }
    Write-Host "Removed $($targets.Count) obsolete game-local editor targets."
}

[pscustomobject][ordered]@{
    mode = $mode
    cache_editor_enabled = $false
    target_count = $targets.Count
    target_bytes = $totalBytes
    targets = $targets
}
