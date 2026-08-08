param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Write-FixtureFile([string]$PathValue, [string]$Text) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $PathValue) -Force | Out-Null
    [IO.File]::WriteAllText($PathValue, $Text, (New-Object Text.UTF8Encoding($false)))
}

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'pokemonautochess-cooked-pruner-test-' + [Guid]::NewGuid().ToString('N'))
$pruner = Join-Path $PSScriptRoot 'prune_unreferenced_cooked_objects.ps1'
try {
    $objectsRoot = Join-Path $tempRoot 'content\phlosion\objects'
    $current = Join-Path $objectsRoot 'fixture-1111111111111111\fixture.phlo'
    $superseded = Join-Path $objectsRoot 'fixture-2222222222222222\fixture.phlo'
    $legacy = Join-Path $objectsRoot 'legacy-3333333333333333\legacy.phlo'
    $unclassified = Join-Path $objectsRoot 'unclassified-4444444444444444\keep.phlo'
    $environment = Join-Path $objectsRoot 'environment\keep.phlo'
    foreach ($path in @($current, $superseded, $legacy, $unclassified, $environment)) {
        Write-FixtureFile $path 'fixture'
    }
    $manifest = [ordered]@{
        schema_version = 2
        kind = 'phlosion_cook_manifest'
        pokemon = @([ordered]@{
            source = 'assets/models/fixture.phmodel'
            object = 'content/phlosion/objects/fixture-1111111111111111/fixture.phlo'
        })
        staged_imports = @()
        runtime_auxiliary_objects = @()
    }
    $catalog = [ordered]@{
        retained_review_sources = @([ordered]@{
            id = 'legacy/fixture'
            legacy_cooked_identities = @('legacy')
        })
    }
    Write-FixtureFile (Join-Path $tempRoot 'content\phlosion\cook_manifest.json') ($manifest | ConvertTo-Json -Depth 6)
    Write-FixtureFile (Join-Path $tempRoot 'config\assets\asset_catalog.json') ($catalog | ConvertTo-Json -Depth 6)

    $plan = & $pruner -GameRoot $tempRoot
    Assert-Condition ($plan.candidate_count -eq 2) 'Pruner did not isolate superseded and catalogued legacy objects.'
    Assert-Condition (Test-Path -LiteralPath $superseded) 'Pruner dry run changed cooked content.'
    $result = & $pruner -GameRoot $tempRoot -Apply
    Assert-Condition ($result.removed_count -eq 2) 'Pruner apply removed an unexpected number of objects.'
    Assert-Condition (-not (Test-Path -LiteralPath $superseded)) 'Pruner retained the superseded object.'
    Assert-Condition (-not (Test-Path -LiteralPath $legacy)) 'Pruner retained the catalogued legacy object.'
    Assert-Condition (Test-Path -LiteralPath $current) 'Pruner removed a manifest-owned object.'
    Assert-Condition (Test-Path -LiteralPath $unclassified) 'Pruner removed an unclassified review object.'
    Assert-Condition (Test-Path -LiteralPath $environment) 'Pruner removed environment content.'
    $idempotent = & $pruner -GameRoot $tempRoot
    Assert-Condition ($idempotent.candidate_count -eq 0) 'Pruner is not idempotent.'
    Write-Host '[UnreferencedCookedObjectPrunerTest] PASS'
} finally {
    $resolvedTempRoot = [IO.Path]::GetFullPath($tempRoot)
    $resolvedSystemTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolvedTempRoot.StartsWith($resolvedSystemTemp, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedTempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
