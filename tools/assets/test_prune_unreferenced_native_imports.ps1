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
    'pokemonautochess-native-import-pruner-test-' + [Guid]::NewGuid().ToString('N'))
$pruner = Join-Path $PSScriptRoot 'prune_unreferenced_native_imports.ps1'
try {
    $modelsRoot = Join-Path $tempRoot 'assets\models'
    $ownedStem = '0001_Bulbasaur_SV'
    $supersededStem = '0063_Abra_PLA'
    $unselectedStem = '0064_Kadabra_PLA'
    $reviewStem = '0063_Abra'
    foreach ($stem in @($ownedStem, $supersededStem, $unselectedStem, $reviewStem)) {
        Write-FixtureFile (Join-Path $modelsRoot "$stem.phmodel") 'model'
        Write-FixtureFile (Join-Path $modelsRoot "$stem.animset.json") '{}'
        Write-FixtureFile (Join-Path $modelsRoot "${stem}_textures\texture.bin") 'texture'
    }
    $recipe = [ordered]@{
        schema = 'phlosion-gamefreak-import-recipe-v1'
        imports = @([ordered]@{
            speciesId = 1
            speciesName = 'Bulbasaur'
            genderLabel = 'unisex'
            outputs = @(
                [ordered]@{ appearance = 'regular'; stem = $ownedStem },
                [ordered]@{ appearance = 'regular'; stem = $unselectedStem }
            )
        })
    }
    $catalog = [ordered]@{
        schema_version = 1
        kind = 'pokemon_autochess_asset_catalog'
        native_import_sets = @([ordered]@{
            recipe = 'tools/assets/fixture_recipe.json'
            selection = 'include_stems'
            stems = @($ownedStem)
        })
        explicit_native_models = @()
    }
    Write-FixtureFile (Join-Path $tempRoot 'tools\assets\fixture_recipe.json') ($recipe | ConvertTo-Json -Depth 8)
    Write-FixtureFile (Join-Path $tempRoot 'config\assets\asset_catalog.json') ($catalog | ConvertTo-Json -Depth 8)

    $plan = & $pruner -GameRoot $tempRoot
    Assert-Condition ($plan.candidate_count -eq 2) 'Pruner did not isolate both canonical unowned imports.'
    Assert-Condition (Test-Path -LiteralPath (Join-Path $modelsRoot "$supersededStem.phmodel")) 'Dry run changed native imports.'
    $result = & $pruner -GameRoot $tempRoot -Apply
    Assert-Condition ($result.removed_count -eq 2) 'Pruner apply removed an unexpected number of native imports.'
    Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $modelsRoot "$supersededStem.phmodel"))) 'Pruner retained a superseded import.'
    Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $modelsRoot "${unselectedStem}_textures"))) 'Pruner retained an unselected import texture directory.'
    Assert-Condition (Test-Path -LiteralPath (Join-Path $modelsRoot "$ownedStem.phmodel")) 'Pruner removed a catalog-owned import.'
    Assert-Condition (Test-Path -LiteralPath (Join-Path $modelsRoot "$reviewStem.phmodel")) 'Pruner removed an unclassified review model.'
    $idempotent = & $pruner -GameRoot $tempRoot
    Assert-Condition ($idempotent.candidate_count -eq 0) 'Pruner is not idempotent.'
    Write-Host '[UnreferencedNativeImportPrunerTest] PASS'
} finally {
    $resolvedTempRoot = [IO.Path]::GetFullPath($tempRoot)
    $resolvedSystemTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolvedTempRoot.StartsWith($resolvedSystemTemp, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedTempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
