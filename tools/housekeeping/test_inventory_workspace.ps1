param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot 'HousekeepingInventory.psm1') -Force

function Assert-Condition {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) { throw $Message }
}

function Assert-CookedDuplicateShape {
    param(
        [object]$Summary,
        [string]$ExpectedMode
    )
    Assert-Condition ($Summary.mode -eq $ExpectedMode) `
        "Unexpected cooked-duplicate mode: $($Summary.mode)"
    foreach ($property in @(
            'file_count',
            'total_bytes',
            'unique_bytes',
            'duplicate_byte_budget',
            'duplicate_budget_exceeded',
            'duplicate_group_count',
            'duplicate_file_count',
            'redundant_bytes',
            'candidate_group_count',
            'redundant_bytes_upper_bound',
            'intentional_semantic_partition_bytes',
            'unexpected_redundant_bytes',
            'groups')) {
        Assert-Condition ($null -ne
            $Summary.PSObject.Properties[$property]) `
            "Cooked-duplicate summary is missing '$property'."
    }
}

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'pokemonautochess-inventory-test-' +
    [Guid]::NewGuid().ToString('N'))
$emptyRoot = Join-Path $tempRoot 'empty'
$populatedRoot = Join-Path $tempRoot 'populated'
New-Item -ItemType Directory -Path $emptyRoot | Out-Null
New-Item -ItemType Directory `
    -Path (Join-Path $populatedRoot 'content\phlosion\objects\one'), `
          (Join-Path $populatedRoot 'content\phlosion\objects\two') | Out-Null

try {
    [IO.File]::WriteAllBytes(
        (Join-Path $populatedRoot 'content\phlosion\objects\one\a.bin'),
        [byte[]](1, 2, 3, 4))
    [IO.File]::WriteAllBytes(
        (Join-Path $populatedRoot 'content\phlosion\objects\two\b.bin'),
        [byte[]](5, 6, 7, 8))

    $module = Get-Module HousekeepingInventory
    $emptyFast = & $module {
        param($Root)
        Get-CookedFileDuplicates -GameRoot $Root -Fast
    } $emptyRoot
    $populatedFast = & $module {
        param($Root)
        Get-CookedFileDuplicates -GameRoot $Root -Fast
    } $populatedRoot
    $emptyFull = & $module {
        param($Root)
        Get-CookedFileDuplicates -GameRoot $Root
    } $emptyRoot

    Assert-CookedDuplicateShape $emptyFast 'size_candidates_only'
    Assert-CookedDuplicateShape $populatedFast 'size_candidates_only'
    Assert-CookedDuplicateShape $emptyFull 'verified_sha256'
    Assert-Condition ($populatedFast.file_count -eq 2) `
        'Fast inventory should count every cooked fixture file.'
    Assert-Condition ($populatedFast.candidate_group_count -eq 1) `
        'Fast inventory should report the equal-size candidate group.'
    Assert-Condition ($null -eq
        $populatedFast.unexpected_redundant_bytes) `
        'Fast inventory must report exact duplicate classification as unavailable.'

    Write-Host '[HousekeepingInventoryTest] PASS'
} finally {
    $resolvedTempRoot = [IO.Path]::GetFullPath($tempRoot)
    $resolvedSystemTemp = [IO.Path]::GetFullPath(
        [IO.Path]::GetTempPath())
    if ($resolvedTempRoot.StartsWith(
            $resolvedSystemTemp,
            [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedTempRoot `
            -Recurse -Force -ErrorAction SilentlyContinue
    }
}
