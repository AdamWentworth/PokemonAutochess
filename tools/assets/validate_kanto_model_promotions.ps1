[CmdletBinding()]
param(
    [string]$GameRoot = '',
    [string]$CatalogPath = 'config/assets/asset_catalog.json',
    [string]$PromotionPath = '',
    [string]$CookManifestPath = 'content/phlosion/cook_manifest.json'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-FullPath([string]$PathValue) {
    return [IO.Path]::GetFullPath($PathValue).TrimEnd('\', '/')
}

function Normalize-ProjectPath([string]$PathValue) {
    return $PathValue.Replace('\', '/').TrimStart('./')
}

function Resolve-ProjectPath([string]$Root, [string]$RelativePath) {
    if ([string]::IsNullOrWhiteSpace($RelativePath) -or
        [IO.Path]::IsPathRooted($RelativePath)) {
        throw "Project path must be non-empty and relative: $RelativePath"
    }
    $fullPath = Resolve-FullPath (Join-Path $Root $RelativePath)
    $rootPrefix = (Resolve-FullPath $Root) + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Project path escapes the game root: $RelativePath"
    }
    return $fullPath
}

function Get-OptionalProperty($Object, [string]$Name, $DefaultValue) {
    if ($null -ne $Object -and $Object.PSObject.Properties.Name -contains $Name) {
        return $Object.$Name
    }
    return $DefaultValue
}

function Read-ProjectJson([string]$Root, [string]$RelativePath) {
    $fullPath = Resolve-ProjectPath $Root $RelativePath
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Required project JSON is missing: $RelativePath"
    }
    return Get-Content -LiteralPath $fullPath -Raw | ConvertFrom-Json
}

if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $PSScriptRoot '..\..'
}
$GameRoot = Resolve-FullPath $GameRoot
$CatalogPath = Normalize-ProjectPath $CatalogPath
$CookManifestPath = Normalize-ProjectPath $CookManifestPath

$catalog = Read-ProjectJson $GameRoot $CatalogPath
if ([string](Get-OptionalProperty $catalog 'kind' '') -ne 'pokemon_autochess_asset_catalog' -or
    [int](Get-OptionalProperty $catalog 'schema_version' 0) -ne 1) {
    throw "Unsupported asset catalog: $CatalogPath"
}

$catalogPromotionPath = Normalize-ProjectPath ([string](Get-OptionalProperty $catalog 'promotion_registry' ''))
if ([string]::IsNullOrWhiteSpace($catalogPromotionPath)) {
    throw "Asset catalog does not declare promotion_registry: $CatalogPath"
}
if ([string]::IsNullOrWhiteSpace($PromotionPath)) {
    $PromotionPath = $catalogPromotionPath
} else {
    $PromotionPath = Normalize-ProjectPath $PromotionPath
    if ($PromotionPath -cne $catalogPromotionPath) {
        throw "Promotion path '$PromotionPath' disagrees with catalog authority '$catalogPromotionPath'."
    }
}

$registry = Read-ProjectJson $GameRoot $PromotionPath
if ([string](Get-OptionalProperty $registry 'schema' '') -ne
        'pokemon-autochess-kanto-model-promotions-v1') {
    throw "Unsupported Kanto model promotion registry: $PromotionPath"
}
if ((Normalize-ProjectPath ([string](Get-OptionalProperty $registry 'catalog' ''))) -cne $CatalogPath) {
    throw "Promotion registry does not point back to its asset catalog: $PromotionPath"
}
if ([int](Get-OptionalProperty $registry.dex_range 'first' 0) -ne 1 -or
    [int](Get-OptionalProperty $registry.dex_range 'last' 0) -ne 151) {
    throw 'Kanto promotion registry must cover National Dex IDs 1 through 151.'
}
if ([string](Get-OptionalProperty $registry 'comparison_policy' '') -ne
        'retain_catalogued_non_promoted_outputs' -or
    [string](Get-OptionalProperty $registry 'optional_form_policy' '') -ne
        'retain_separately_from_base_species') {
    throw 'Kanto promotion registry must preserve comparison outputs and keep optional forms separate.'
}

$catalogRecipes = @{}
$catalogStems = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
foreach ($set in @($catalog.native_import_sets)) {
    $recipePath = Normalize-ProjectPath ([string](Get-OptionalProperty $set 'recipe' ''))
    if ($catalogRecipes.ContainsKey($recipePath)) {
        throw "Asset catalog repeats native import recipe: $recipePath"
    }
    $recipe = Read-ProjectJson $GameRoot $recipePath
    $catalogRecipes[$recipePath] = $recipe

    $selection = [string](Get-OptionalProperty $set 'selection' '')
    $includedStems = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    if ($selection -eq 'include_stems') {
        foreach ($stem in @($set.stems)) {
            if (-not $includedStems.Add([string]$stem)) {
                throw "Asset catalog repeats selected stem: $stem"
            }
        }
    } elseif ($selection -ne 'all_outputs') {
        throw "Unsupported native import selection '$selection' for $recipePath"
    }

    foreach ($import in @($recipe.imports)) {
        foreach ($output in @($import.outputs)) {
            $stem = [string](Get-OptionalProperty $output 'stem' '')
            if ($selection -eq 'include_stems' -and -not $includedStems.Contains($stem)) {
                continue
            }
            if ([string]::IsNullOrWhiteSpace($stem) -or -not $catalogStems.Add($stem)) {
                throw "Asset catalog contains an empty or duplicate native stem: $stem"
            }
        }
    }
    if ($selection -eq 'include_stems') {
        $missing = @($includedStems | Where-Object { -not $catalogStems.Contains($_) })
        if ($missing.Count -ne 0) {
            throw "Asset catalog selects undeclared recipe stems: $($missing -join ', ')"
        }
    }
}

$recipesBySource = @{}
foreach ($source in @($registry.sources)) {
    $sourceId = [string](Get-OptionalProperty $source 'id' '')
    $recipePath = Normalize-ProjectPath ([string](Get-OptionalProperty $source 'recipe' ''))
    if ([string]::IsNullOrWhiteSpace($sourceId) -or $recipesBySource.ContainsKey($sourceId)) {
        throw "Promotion registry contains an empty or duplicate source id: $sourceId"
    }
    if (-not $catalogRecipes.ContainsKey($recipePath)) {
        throw "Promotion source '$sourceId' is not selected by the asset catalog: $recipePath"
    }
    $recipesBySource[$sourceId] = [pscustomobject]@{
        path = $recipePath
        document = $catalogRecipes[$recipePath]
    }
}

$promotions = @($registry.promotions)
if ($promotions.Count -ne 151) {
    throw "Kanto promotion registry requires exactly 151 rows; found $($promotions.Count)."
}

$speciesIds = New-Object 'System.Collections.Generic.HashSet[int]'
$promotedStems = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
$sourceSpeciesCounts = @{}
foreach ($promotion in $promotions) {
    $speciesId = [int](Get-OptionalProperty $promotion 'species_id' 0)
    $speciesName = [string](Get-OptionalProperty $promotion 'species_name' '')
    $sourceId = [string](Get-OptionalProperty $promotion 'source' '')
    $status = [string](Get-OptionalProperty $promotion 'status' '')
    if ($speciesId -lt 1 -or $speciesId -gt 151 -or -not $speciesIds.Add($speciesId)) {
        throw "Promotion registry contains an invalid or duplicate species id: $speciesId"
    }
    if ([string]::IsNullOrWhiteSpace($speciesName)) {
        throw "Promotion registry species #$speciesId has no species_name."
    }
    if ($status -ne 'accepted_for_vertical_slice') {
        throw "Promotion registry species #$speciesId has unsupported status '$status'."
    }
    if (-not $recipesBySource.ContainsKey($sourceId)) {
        throw "Promotion registry species #$speciesId names unknown source '$sourceId'."
    }

    $sourceRecord = $recipesBySource[$sourceId]
    $baseImports = @($sourceRecord.document.imports | Where-Object {
        $form = Get-OptionalProperty $_ 'form' 0
        [int]$_.speciesId -eq $speciesId -and ($null -eq $form -or [int]$form -eq 0)
    })
    if ($baseImports.Count -eq 0) {
        throw "Promoted source '$sourceId' has no base-form import for species #$speciesId."
    }
    foreach ($import in $baseImports) {
        if ([string]$import.speciesName -cne $speciesName) {
            throw "Promotion species name '$speciesName' disagrees with $($sourceRecord.path): '$($import.speciesName)'."
        }
        $outputs = @($import.outputs)
        foreach ($appearance in @('regular', 'shiny')) {
            $matching = @($outputs | Where-Object { [string]$_.appearance -eq $appearance })
            if ($matching.Count -ne 1) {
                throw "Species #$speciesId source '$sourceId' requires exactly one $appearance output per base import."
            }
        }
        if ($outputs.Count -ne 2) {
            throw "Species #$speciesId source '$sourceId' base import contains unsupported appearance outputs."
        }
        foreach ($output in $outputs) {
            $stem = [string]$output.stem
            if (-not $catalogStems.Contains($stem)) {
                throw "Promoted output is not retained by the asset catalog: $stem"
            }
            if (-not $promotedStems.Add($stem)) {
                throw "Promotion registry resolves two base identities to the same stem: $stem"
            }
            foreach ($extension in @('.phmodel', '.animset.json')) {
                $sourcePath = "assets/models/$stem$extension"
                if (-not (Test-Path -LiteralPath (Resolve-ProjectPath $GameRoot $sourcePath) -PathType Leaf)) {
                    throw "Promoted source asset is missing: $sourcePath"
                }
            }
        }
    }
    if (-not $sourceSpeciesCounts.ContainsKey($sourceId)) {
        $sourceSpeciesCounts[$sourceId] = 0
    }
    $sourceSpeciesCounts[$sourceId]++
}

if (($speciesIds | Sort-Object) -join ',' -cne ((1..151) -join ',')) {
    throw 'Kanto promotion registry does not contain every National Dex ID exactly once.'
}

$pokemonConfigPath = Normalize-ProjectPath ([string]$catalog.pokemon_config)
$pokemonConfig = Read-ProjectJson $GameRoot $pokemonConfigPath
$activeStems = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
foreach ($pokemon in $pokemonConfig.PSObject.Properties) {
    $modelPaths = @()
    $model = [string](Get-OptionalProperty $pokemon.Value 'model' '')
    if (-not [string]::IsNullOrWhiteSpace($model)) {
        $modelPaths += $model
    }
    $variants = Get-OptionalProperty $pokemon.Value 'modelVariants' $null
    if ($null -ne $variants) {
        foreach ($variant in $variants.PSObject.Properties) {
            $modelPaths += [string]$variant.Value
        }
    }
    foreach ($modelPath in $modelPaths) {
        if ([IO.Path]::GetExtension($modelPath) -cne '.phmodel') {
            throw "Configured Pokemon '$($pokemon.Name)' uses a non-native model: $modelPath"
        }
        $stem = [IO.Path]::GetFileNameWithoutExtension($modelPath)
        if (-not $promotedStems.Contains($stem)) {
            throw "Configured Pokemon '$($pokemon.Name)' uses non-promoted model: $modelPath"
        }
        [void]$activeStems.Add($stem)
    }
}

$cookManifest = Read-ProjectJson $GameRoot $CookManifestPath
if ([string](Get-OptionalProperty $cookManifest 'kind' '') -ne 'phlosion_cook_manifest' -or
    [int](Get-OptionalProperty $cookManifest 'schema_version' 0) -ne 2) {
    throw "Unsupported cook manifest: $CookManifestPath"
}
$manifestBySource = @{}
$nativeManifestEntries = @($cookManifest.pokemon) + @($cookManifest.staged_imports)
foreach ($entry in $nativeManifestEntries) {
    $sourcePath = Normalize-ProjectPath ([string](Get-OptionalProperty $entry 'source' ''))
    if ([string]::IsNullOrWhiteSpace($sourcePath) -or $manifestBySource.ContainsKey($sourcePath)) {
        throw "Cook manifest contains an empty or duplicate native source: $sourcePath"
    }
    $manifestBySource[$sourcePath] = $entry
    $objectPath = Normalize-ProjectPath ([string](Get-OptionalProperty $entry 'object' ''))
    if (-not (Test-Path -LiteralPath (Resolve-ProjectPath $GameRoot $objectPath) -PathType Leaf)) {
        throw "Cooked native object is missing: $objectPath"
    }
}

foreach ($stem in $catalogStems) {
    $sourcePath = "assets/models/$stem.phmodel"
    if (-not $manifestBySource.ContainsKey($sourcePath)) {
        throw "Catalogued native model is absent from the current cook manifest: $sourcePath"
    }
}
if ($manifestBySource.Count -ne $catalogStems.Count) {
    $unexpected = @($manifestBySource.Keys | Where-Object {
        $stem = [IO.Path]::GetFileNameWithoutExtension($_)
        -not $catalogStems.Contains($stem)
    })
    throw "Cook manifest native set disagrees with the asset catalog. Unexpected: $($unexpected -join ', ')"
}
foreach ($stem in $promotedStems) {
    $sourcePath = "assets/models/$stem.phmodel"
    if (-not $manifestBySource.ContainsKey($sourcePath)) {
        throw "Promoted model is not cooked: $sourcePath"
    }
}

$sourceSummary = @($sourceSpeciesCounts.Keys | Sort-Object | ForEach-Object {
    "$_=$($sourceSpeciesCounts[$_])"
}) -join ', '
$comparisonCount = $catalogStems.Count - $promotedStems.Count
Write-Host "Kanto model promotions valid: 151 species, $($promotedStems.Count) promoted model variants, $comparisonCount retained non-promoted variants, $($activeStems.Count) configured variants. Sources: $sourceSummary."
