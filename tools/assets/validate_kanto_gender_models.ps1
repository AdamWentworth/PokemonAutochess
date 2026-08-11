[CmdletBinding()]
param(
    [string]$GameRoot = '',
    [string]$CatalogPath = 'config/assets/asset_catalog.json',
    [string]$PolicyPath = 'tools/assets/kanto_gender_model_policy.json'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-FullPath([string]$PathValue) {
    return [IO.Path]::GetFullPath($PathValue).TrimEnd('\', '/')
}

function Resolve-ProjectPath([string]$Root, [string]$RelativePath) {
    if ([IO.Path]::IsPathRooted($RelativePath)) {
        throw "Project path must be relative: $RelativePath"
    }
    $fullPath = Resolve-FullPath (Join-Path $Root $RelativePath)
    $rootPrefix = (Resolve-FullPath $Root) + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Project path escapes the game root: $RelativePath"
    }
    return $fullPath
}

function Normalize-ProjectPath([string]$PathValue) {
    return $PathValue.Replace('\', '/').TrimStart('./')
}

if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $PSScriptRoot '..\..'
}
$GameRoot = Resolve-FullPath $GameRoot
$catalogFullPath = Resolve-ProjectPath $GameRoot $CatalogPath
$policyFullPath = Resolve-ProjectPath $GameRoot $PolicyPath
if (-not (Test-Path -LiteralPath $catalogFullPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $policyFullPath -PathType Leaf)) {
    throw 'The asset catalog and Kanto gender-model policy are required.'
}

$catalog = Get-Content -LiteralPath $catalogFullPath -Raw | ConvertFrom-Json
$policy = Get-Content -LiteralPath $policyFullPath -Raw | ConvertFrom-Json
if ([string]$catalog.kind -ne 'pokemon_autochess_asset_catalog' -or
    [int]$catalog.schema_version -ne 1) {
    throw "Unsupported asset catalog: $catalogFullPath"
}
if ([string]$policy.schema -ne 'pokemon-autochess-kanto-gender-model-policy-v1') {
    throw "Unsupported gender-model policy: $policyFullPath"
}

$requiredAppearances = @($policy.requiredAppearances | ForEach-Object { [string]$_ })
if ($requiredAppearances.Count -ne 2 -or
    $requiredAppearances -notcontains 'regular' -or
    $requiredAppearances -notcontains 'shiny') {
    throw 'The gender-model policy must require regular and shiny appearances.'
}

$selectedImports = @()
foreach ($set in @($catalog.native_import_sets)) {
    $recipeRelativePath = Normalize-ProjectPath ([string]$set.recipe)
    $recipeFullPath = Resolve-ProjectPath $GameRoot $recipeRelativePath
    if (-not (Test-Path -LiteralPath $recipeFullPath -PathType Leaf)) {
        throw "Catalogued import recipe is missing: $recipeRelativePath"
    }
    $recipe = Get-Content -LiteralPath $recipeFullPath -Raw | ConvertFrom-Json
    $selectedStems = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    if ([string]$set.selection -eq 'include_stems') {
        foreach ($stem in @($set.stems)) {
            [void]$selectedStems.Add([string]$stem)
        }
    } elseif ([string]$set.selection -ne 'all_outputs') {
        throw "Unsupported import selection '$($set.selection)' in $CatalogPath"
    }

    foreach ($import in @($recipe.imports)) {
        $outputs = @($import.outputs | Where-Object {
            [string]$set.selection -eq 'all_outputs' -or $selectedStems.Contains([string]$_.stem)
        })
        if ($outputs.Count -eq 0) { continue }
        $selectedImports += [pscustomobject][ordered]@{
            recipe = $recipeRelativePath
            species_id = [int]$import.speciesId
            species_name = [string]$import.speciesName
            gender_label = ([string]$import.genderLabel).ToLowerInvariant()
            outputs = $outputs
        }
    }
}

$policyIds = New-Object 'System.Collections.Generic.HashSet[int]'
$qualifiedCount = 0
$pendingCount = 0
foreach ($entry in @($policy.species)) {
    $speciesId = [int]$entry.speciesId
    if ($speciesId -lt 1 -or $speciesId -gt 151 -or -not $policyIds.Add($speciesId)) {
        throw "Invalid or duplicate Kanto gender-policy species id: $speciesId"
    }
    $speciesImports = @($selectedImports | Where-Object { $_.species_id -eq $speciesId })
    if ($speciesImports.Count -eq 0) {
        if ([string]$entry.status -ne 'pending_import') {
            throw "Qualified gender-policy species #$speciesId has no selected import."
        }
        $pendingCount++
        continue
    }

    if ([string]$entry.status -ne 'qualified') {
        throw "Imported gender-policy species #$speciesId is still marked '$($entry.status)'."
    }
    if (-not ($entry.PSObject.Properties.Name -contains 'authoritativeRecipe')) {
        throw "Qualified gender-policy species #$speciesId has no authoritativeRecipe."
    }
    $authoritativeRecipe = Normalize-ProjectPath ([string]$entry.authoritativeRecipe)
    $unexpectedRecipes = @($speciesImports | Where-Object { $_.recipe -ne $authoritativeRecipe })
    if ($unexpectedRecipes.Count -gt 0) {
        $recipes = @($speciesImports.recipe | Sort-Object -Unique) -join ', '
        throw "Gender-policy species #$speciesId is selected from $recipes; expected only $authoritativeRecipe."
    }

    foreach ($genderLabel in @('male', 'female')) {
        $genderImports = @($speciesImports | Where-Object { $_.gender_label -eq $genderLabel })
        if ($genderImports.Count -ne 1) {
            throw "Gender-policy species #$speciesId requires exactly one selected $genderLabel import; found $($genderImports.Count)."
        }
        $appearances = @($genderImports[0].outputs | ForEach-Object { [string]$_.appearance })
        foreach ($appearance in $requiredAppearances) {
            if (@($appearances | Where-Object { $_ -eq $appearance }).Count -ne 1) {
                throw "Gender-policy species #$speciesId $genderLabel requires exactly one $appearance output."
            }
        }
        foreach ($output in @($genderImports[0].outputs)) {
            $stem = [string]$output.stem
            if ($genderLabel -eq 'female' -and $stem -notmatch '_Female(?:_|$)') {
                throw "Female output does not carry the canonical _Female identity: $stem"
            }
            if ($genderLabel -eq 'male' -and $stem -match '_Female(?:_|$)') {
                throw "Male output incorrectly carries the canonical _Female identity: $stem"
            }
        }
    }
    $otherLabels = @($speciesImports | Where-Object { $_.gender_label -notin @('male', 'female') })
    if ($otherLabels.Count -gt 0) {
        throw "Gender-policy species #$speciesId has selected imports without male/female labels."
    }
    $qualifiedCount++
}

$expectedSpeciesIds = @(
    3, 12, 19, 20, 25, 26, 41, 42, 44, 45, 64, 65,
    84, 85, 97, 111, 112, 118, 119, 123, 129, 130, 133
)
$actualSpeciesIds = @($policyIds | Sort-Object)
if (($actualSpeciesIds -join ',') -ne (($expectedSpeciesIds | Sort-Object) -join ',')) {
    throw 'The gender-model policy must contain the complete Kanto sex-dimorphism species set.'
}

$selectedPolicySpecies = @($selectedImports | Where-Object { $policyIds.Contains($_.species_id) })
Write-Host "Kanto gender-model policy valid: $qualifiedCount qualified, $pendingCount pending, $($selectedPolicySpecies.Count) selected sex-specific import records."
