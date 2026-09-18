param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$validator = Join-Path $PSScriptRoot 'validate_kanto_model_promotions.ps1'
$tempParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
$fixtureRoot = Join-Path $tempParent ('pac-promotion-contract-' + [Guid]::NewGuid().ToString('N'))

function Write-Fixture([string]$Path, $Value) {
    $target = Join-Path $fixtureRoot $Path
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $target))
    [IO.File]::WriteAllText($target, ($Value | ConvertTo-Json -Depth 12))
}

function Assert-Rejected([string]$Expected) {
    $message = ''
    try { & $validator -GameRoot $fixtureRoot } catch { $message = $_.Exception.Message }
    if ($message -notlike "*$Expected*") {
        throw "Expected rejection '$Expected'; got '$message'."
    }
}

try {
    $catalogPath = 'config/assets/asset_catalog.json'
    $registryPath = 'config/assets/promotions.json'
    $packagePath = 'config/assets/package.json'
    $catalog = [ordered]@{
        kind = 'pokemon_autochess_asset_catalog'; schema_version = 1
        promotion_registry = $registryPath; pokemon_config = 'config/pokemon_config.json'
        native_import_sets = @(@{ selection = 'all_outputs'; recipe = $packagePath })
        explicit_native_models = @()
    }
    $imports = @(); $promotions = @(); $entries = @()
    foreach ($id in 1..151) {
        $outputs = @(); $models = @()
        foreach ($appearance in @('regular', 'shiny')) {
            $stem = "fixture_${id}_$appearance"
            $outputs += @{stem = $stem; appearance = $appearance}
            $models += @{stem = $stem; appearance = $appearance; variant = 'unisex'}
            Write-Fixture "assets/models/$stem.phmodel" @{}
            Write-Fixture "assets/models/$stem.animset.json" @{}
            Write-Fixture "content/phlosion/objects/$stem.phlo" @{}
            $entries += @{source = "assets/models/$stem.phmodel"; object = "content/phlosion/objects/$stem.phlo"}
        }
        $imports += @{speciesId = $id; speciesName = "Species$id"; genderLabel = 'unisex'; outputs = $outputs}
        $promotions += @{species_id = $id; species_name = "Species$id"; status = 'accepted_for_vertical_slice'; models = $models}
    }
    $registry = @{
        schema = 'pokemon-autochess-kanto-model-promotions-v2'; catalog = $catalogPath
        package = $packagePath; dex_range = @{first = 1; last = 151}; promotions = $promotions
    }
    Write-Fixture $packagePath @{schema = 'pokemon-autochess-native-model-package-v1'; sourceGame = 'external-research-package'; imports = $imports}
    $manifest = @{kind = 'phlosion_cook_manifest'; schema_version = 2; pokemon = @(); staged_imports = $entries}
    $pokemon = @{fixture = @{model = 'assets/models/fixture_1_regular.phmodel'; modelVariants = @{regular = 'assets/models/fixture_1_regular.phmodel'; shiny = 'assets/models/fixture_1_shiny.phmodel'}}}
    Write-Fixture $catalogPath $catalog
    Write-Fixture $registryPath $registry
    Write-Fixture 'config/pokemon_config.json' $pokemon
    Write-Fixture 'content/phlosion/cook_manifest.json' $manifest
    & $validator -GameRoot $fixtureRoot

    # Authored pairs can replace an imported pair without editing that package.
    foreach ($appearance in @('regular', 'shiny')) {
        $stem = "authored_1_$appearance"
        $catalog.explicit_native_models += @{
            stem = $stem; scope = 'staged_import'; source = "assets/models/$stem.phmodel"
            animset = "assets/models/$stem.animset.json"; species_id = 1
            species_name = 'Species1'; variant = 'unisex'; appearance = $appearance
        }
        Write-Fixture "assets/models/$stem.phmodel" @{}
        Write-Fixture "assets/models/$stem.animset.json" @{}
        Write-Fixture "content/phlosion/objects/$stem.phlo" @{}
        $manifest.staged_imports += @{source = "assets/models/$stem.phmodel"; object = "content/phlosion/objects/$stem.phlo"}
    }
    $registry.promotions[0].models = @(@{stem = 'authored_1_regular'; appearance = 'regular'; variant = 'unisex'}, @{stem = 'authored_1_shiny'; appearance = 'shiny'; variant = 'unisex'})
    $pokemon.fixture.model = 'assets/models/authored_1_regular.phmodel'
    $pokemon.fixture.modelVariants = @{regular = $pokemon.fixture.model; shiny = 'assets/models/authored_1_shiny.phmodel'}
    Write-Fixture $catalogPath $catalog
    Write-Fixture $registryPath $registry
    Write-Fixture 'config/pokemon_config.json' $pokemon
    Write-Fixture 'content/phlosion/cook_manifest.json' $manifest
    & $validator -GameRoot $fixtureRoot

    $catalog.explicit_native_models[0].species_id = 2
    Write-Fixture $catalogPath $catalog
    Assert-Rejected 'Promoted identity disagrees'
    $catalog.explicit_native_models[0].species_id = 1
    $catalog.explicit_native_models[0].Remove('appearance')
    Write-Fixture $catalogPath $catalog
    Assert-Rejected 'incomplete or invalid promotion identity'
    foreach ($key in @('species_id', 'species_name', 'variant')) { $catalog.explicit_native_models[0].Remove($key) }
    Write-Fixture $catalogPath $catalog
    Assert-Rejected 'no identified catalog source'
    $catalog.explicit_native_models[0].species_id = 1
    $catalog.explicit_native_models[0].species_name = 'Species1'
    $catalog.explicit_native_models[0].variant = 'unisex'
    $catalog.explicit_native_models[0].appearance = 'regular'
    $catalog.explicit_native_models += $catalog.explicit_native_models[0]
    Write-Fixture $catalogPath $catalog
    Assert-Rejected 'duplicate explicit stem'
    $catalog.explicit_native_models = $catalog.explicit_native_models[0..1]
    Write-Fixture $catalogPath $catalog

    $registry.promotions[0].models = @($registry.promotions[0].models[0])
    Write-Fixture $registryPath $registry
    Assert-Rejected 'requires paired regular and shiny'
    $registry.promotions[0].models += @{stem = 'authored_1_shiny'; appearance = 'shiny'; variant = 'unisex'}
    Write-Fixture $registryPath $registry
    $pokemon.fixture.model = 'assets/models/fixture_1_regular.phmodel'
    Write-Fixture 'config/pokemon_config.json' $pokemon
    Assert-Rejected 'uses non-promoted model'
    $pokemon.fixture.model = 'assets/models/authored_1_regular.phmodel'
    Write-Fixture 'config/pokemon_config.json' $pokemon

    $manifest.staged_imports = $entries
    Write-Fixture 'content/phlosion/cook_manifest.json' $manifest
    Assert-Rejected 'absent from the current cook manifest'
    Write-Host '[KantoModelPromotionsContract] PASS: imported and authored pairs; seven invalid configurations rejected.'
} finally {
    $resolvedFixture = [IO.Path]::GetFullPath($fixtureRoot)
    if ([IO.Path]::GetDirectoryName($resolvedFixture) -cne $tempParent -or
        [IO.Path]::GetFileName($resolvedFixture) -notlike 'pac-promotion-contract-*') {
        throw "Refusing fixture cleanup outside the designated temporary directory: $resolvedFixture"
    }
    Remove-Item -LiteralPath $resolvedFixture -Recurse -Force -ErrorAction SilentlyContinue
}
