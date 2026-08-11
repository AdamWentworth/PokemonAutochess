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
    'pokemonautochess-review-model-pruner-test-' + [Guid]::NewGuid().ToString('N'))
$pruner = Join-Path $PSScriptRoot 'prune_unreferenced_review_models.ps1'
try {
    $modelsRoot = Join-Path $tempRoot 'assets\models'
    foreach ($stem in @('0074_Geodude', '0095_Onix', 'pokeball')) {
        Write-FixtureFile (Join-Path $modelsRoot "$stem.glb") 'model'
        Write-FixtureFile (Join-Path $modelsRoot "$stem.animset.json") '{}'
    }
    $catalog = [ordered]@{
        schema_version = 1
        kind = 'pokemon_autochess_asset_catalog'
        authored_runtime_sources = @([ordered]@{
            source = 'assets/models/pokeball.glb'
        })
        retained_review_sources = @([ordered]@{
            source = 'assets/models/0074_Geodude.glb'
            animset = 'assets/models/0074_Geodude.animset.json'
        })
    }
    Write-FixtureFile (Join-Path $tempRoot 'config\assets\asset_catalog.json') ($catalog | ConvertTo-Json -Depth 6)

    $plan = & $pruner -GameRoot $tempRoot
    Assert-Condition ($plan.candidate_count -eq 1) 'Pruner did not isolate the unowned numbered GLB.'
    Assert-Condition ([string]$plan.candidates[0].stem -eq '0095_Onix') 'Pruner selected the wrong GLB.'
    $result = & $pruner -GameRoot $tempRoot -Apply
    Assert-Condition ($result.removed_count -eq 1) 'Pruner apply removed an unexpected number of GLBs.'
    Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $modelsRoot '0095_Onix.glb'))) 'Pruner retained the unowned GLB.'
    Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $modelsRoot '0095_Onix.animset.json'))) 'Pruner retained the paired unowned animation set.'
    Assert-Condition (Test-Path -LiteralPath (Join-Path $modelsRoot '0074_Geodude.glb')) 'Pruner removed a retained review source.'
    Assert-Condition (Test-Path -LiteralPath (Join-Path $modelsRoot 'pokeball.glb')) 'Pruner removed an authored runtime source.'
    $idempotent = & $pruner -GameRoot $tempRoot
    Assert-Condition ($idempotent.candidate_count -eq 0) 'Pruner is not idempotent.'
    Write-Host '[UnreferencedReviewModelPrunerTest] PASS'
} finally {
    $resolvedTempRoot = [IO.Path]::GetFullPath($tempRoot)
    $resolvedSystemTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolvedTempRoot.StartsWith($resolvedSystemTemp, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedTempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
