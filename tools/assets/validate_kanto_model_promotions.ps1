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
    if (-not $fullPath.StartsWith(
            $rootPrefix,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Project path escapes the game root: $RelativePath"
    }
    return $fullPath
}

function Get-OptionalProperty($Object, [string]$Name, $DefaultValue) {
    if ($null -ne $Object -and
        $Object.PSObject.Properties.Name -contains $Name) {
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
if ([string](Get-OptionalProperty $catalog 'kind' '') -ne
        'pokemon_autochess_asset_catalog' -or
    [int](Get-OptionalProperty $catalog 'schema_version' 0) -ne 1) {
    throw "Unsupported asset catalog: $CatalogPath"
}

$catalogPromotionPath = Normalize-ProjectPath (
    [string](Get-OptionalProperty $catalog 'promotion_registry' ''))
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
        'pokemon-autochess-kanto-model-promotions-v2') {
    throw "Unsupported Kanto model promotion registry: $PromotionPath"
}
if ((Normalize-ProjectPath (
        [string](Get-OptionalProperty $registry 'catalog' ''))) -cne
        $CatalogPath) {
    throw "Promotion registry does not point back to its asset catalog: $PromotionPath"
}
if ([int](Get-OptionalProperty $registry.dex_range 'first' 0) -ne 1 -or
    [int](Get-OptionalProperty $registry.dex_range 'last' 0) -ne 151) {
    throw 'Kanto promotion registry must cover National Dex IDs 1 through 151.'
}

$catalogSets = @($catalog.native_import_sets)
if ($catalogSets.Count -ne 1 -or
    [string](Get-OptionalProperty $catalogSets[0] 'selection' '') -ne
        'all_outputs') {
    throw 'The game catalog must consume one complete external model package.'
}
$packagePath = Normalize-ProjectPath (
    [string](Get-OptionalProperty $catalogSets[0] 'recipe' ''))
if ($packagePath -cne (Normalize-ProjectPath (
        [string](Get-OptionalProperty $registry 'package' '')))) {
    throw 'Promotion registry package disagrees with the asset catalog.'
}
$package = Read-ProjectJson $GameRoot $packagePath
if ([string](Get-OptionalProperty $package 'schema' '') -ne
        'pokemon-autochess-native-model-package-v1' -or
    [string](Get-OptionalProperty $package 'sourceGame' '') -ne
        'external-research-package') {
    throw "Unsupported source-neutral model package: $packagePath"
}

$catalogStems = New-Object 'System.Collections.Generic.HashSet[string]' (
    [StringComparer]::Ordinal)
$packageModels = @{}
foreach ($import in @($package.imports)) {
    $speciesId = [int](Get-OptionalProperty $import 'speciesId' 0)
    $speciesName = [string](Get-OptionalProperty $import 'speciesName' '')
    $variant = [string](Get-OptionalProperty $import 'genderLabel' 'default')
    if ($speciesId -lt 1 -or
        [string]::IsNullOrWhiteSpace($speciesName)) {
        throw 'External model package contains invalid species identity.'
    }
    foreach ($output in @($import.outputs)) {
        $stem = [string](Get-OptionalProperty $output 'stem' '')
        $appearance = [string](Get-OptionalProperty $output 'appearance' '')
        if ([string]::IsNullOrWhiteSpace($stem) -or
            -not $catalogStems.Add($stem)) {
            throw "External model package contains an empty or duplicate stem: $stem"
        }
        if ($appearance -notin @('regular', 'shiny')) {
            throw "External model package contains unsupported appearance '$appearance': $stem"
        }
        $packageModels[$stem] = [pscustomobject]@{
            species_id = $speciesId
            species_name = $speciesName
            variant = $variant
            appearance = $appearance
        }
    }
}
if ($catalogStems.Count -eq 0) {
    throw 'External model package contains no models.'
}

$promotions = @($registry.promotions)
if ($promotions.Count -ne 151) {
    throw "Kanto promotion registry requires exactly 151 rows; found $($promotions.Count)."
}

$speciesIds = New-Object 'System.Collections.Generic.HashSet[int]'
$promotedStems = New-Object 'System.Collections.Generic.HashSet[string]' (
    [StringComparer]::Ordinal)
foreach ($promotion in $promotions) {
    $speciesId = [int](Get-OptionalProperty $promotion 'species_id' 0)
    $speciesName = [string](Get-OptionalProperty $promotion 'species_name' '')
    $status = [string](Get-OptionalProperty $promotion 'status' '')
    if ($speciesId -lt 1 -or $speciesId -gt 151 -or
        -not $speciesIds.Add($speciesId)) {
        throw "Promotion registry contains an invalid or duplicate species id: $speciesId"
    }
    if ([string]::IsNullOrWhiteSpace($speciesName)) {
        throw "Promotion registry species #$speciesId has no species_name."
    }
    if ($status -ne 'accepted_for_vertical_slice') {
        throw "Promotion registry species #$speciesId has unsupported status '$status'."
    }

    $appearanceCounts = @{ regular = 0; shiny = 0 }
    foreach ($model in @($promotion.models)) {
        $stem = [string](Get-OptionalProperty $model 'stem' '')
        $variant = [string](Get-OptionalProperty $model 'variant' 'default')
        $appearance = [string](Get-OptionalProperty $model 'appearance' '')
        if (-not $packageModels.ContainsKey($stem)) {
            throw "Promoted model is absent from the external package: $stem"
        }
        $packageModel = $packageModels[$stem]
        if ([int]$packageModel.species_id -ne $speciesId -or
            [string]$packageModel.species_name -cne $speciesName -or
            [string]$packageModel.variant -cne $variant -or
            [string]$packageModel.appearance -cne $appearance) {
            throw "Promoted identity disagrees with the external package: $stem"
        }
        if (-not $promotedStems.Add($stem)) {
            throw "Promotion registry repeats a model stem: $stem"
        }
        $appearanceCounts[$appearance]++
        foreach ($extension in @('.phmodel', '.animset.json')) {
            $sourcePath = "assets/models/$stem$extension"
            if (-not (Test-Path -LiteralPath (
                    Resolve-ProjectPath $GameRoot $sourcePath) -PathType Leaf)) {
                throw "Promoted source asset is missing: $sourcePath"
            }
        }
    }
    if (($appearanceCounts.regular + $appearanceCounts.shiny) -eq 0) {
        throw "Promotion registry species #$speciesId has no models."
    }
    if ($appearanceCounts.regular -ne $appearanceCounts.shiny) {
        throw "Promotion species #$speciesId requires paired regular and shiny models."
    }
}
if (($speciesIds | Sort-Object) -join ',' -cne ((1..151) -join ',')) {
    throw 'Kanto promotion registry does not contain every National Dex ID exactly once.'
}

$pokemonConfigPath = Normalize-ProjectPath ([string]$catalog.pokemon_config)
$pokemonConfig = Read-ProjectJson $GameRoot $pokemonConfigPath
$activeStems = New-Object 'System.Collections.Generic.HashSet[string]' (
    [StringComparer]::Ordinal)
foreach ($pokemon in $pokemonConfig.PSObject.Properties) {
    $modelPaths = @([string](Get-OptionalProperty $pokemon.Value 'model' ''))
    $variants = Get-OptionalProperty $pokemon.Value 'modelVariants' $null
    if ($null -ne $variants) {
        foreach ($variant in $variants.PSObject.Properties) {
            $modelPaths += [string]$variant.Value
        }
    }
    foreach ($modelPath in @($modelPaths | Where-Object {
            -not [string]::IsNullOrWhiteSpace($_) })) {
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
if ([string](Get-OptionalProperty $cookManifest 'kind' '') -ne
        'phlosion_cook_manifest' -or
    [int](Get-OptionalProperty $cookManifest 'schema_version' 0) -ne 2) {
    throw "Unsupported cook manifest: $CookManifestPath"
}
$manifestBySource = @{}
$nativeManifestEntries = @($cookManifest.pokemon) +
    @($cookManifest.staged_imports)
foreach ($entry in $nativeManifestEntries) {
    $sourcePath = Normalize-ProjectPath (
        [string](Get-OptionalProperty $entry 'source' ''))
    if ([string]::IsNullOrWhiteSpace($sourcePath) -or
        $manifestBySource.ContainsKey($sourcePath)) {
        throw "Cook manifest contains an empty or duplicate native source: $sourcePath"
    }
    $manifestBySource[$sourcePath] = $entry
    $objectPath = Normalize-ProjectPath (
        [string](Get-OptionalProperty $entry 'object' ''))
    if (-not (Test-Path -LiteralPath (
            Resolve-ProjectPath $GameRoot $objectPath) -PathType Leaf)) {
        throw "Cooked native object is missing: $objectPath"
    }
}
foreach ($stem in $catalogStems) {
    $sourcePath = "assets/models/$stem.phmodel"
    if (-not $manifestBySource.ContainsKey($sourcePath)) {
        throw "Packaged native model is absent from the current cook manifest: $sourcePath"
    }
}
if ($manifestBySource.Count -ne $catalogStems.Count) {
    $unexpected = @($manifestBySource.Keys | Where-Object {
        $stem = [IO.Path]::GetFileNameWithoutExtension($_)
        -not $catalogStems.Contains($stem)
    })
    throw "Cook manifest native set disagrees with the model package. Unexpected: $($unexpected -join ', ')"
}

$comparisonCount = $catalogStems.Count - $promotedStems.Count
Write-Host (
    "Kanto model package valid: 151 species, $($promotedStems.Count) " +
    "promoted model variants, $comparisonCount retained comparison variants, " +
    "$($activeStems.Count) configured variants.")
