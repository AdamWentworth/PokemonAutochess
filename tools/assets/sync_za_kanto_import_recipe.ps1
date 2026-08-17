[CmdletBinding()]
param(
    [string]$RecipePath = "",
    [string]$HashListPath = "",
    [string]$PackageRoot = "\\TNAS-98B9\pokemon\Game Files\Switch\Pokemon_Legends_ZA_v2.0.0_Merged_GameFiles\arc",
    [switch]$Check
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Resolve-FullPath([string]$PathValue) {
    return [IO.Path]::GetFullPath($PathValue)
}

function Get-OutputSuffix([int]$SpeciesId, [int]$Form, [int]$Variant) {
    if ($Form -eq 1 -and $Variant -eq 0) {
        return "Female"
    }
    if ($Form -eq 51) {
        if ($SpeciesId -in @(6, 26, 150)) {
            return "MegaX"
        }
        return "Mega"
    }
    if ($Form -eq 52) {
        if ($SpeciesId -in @(6, 26, 150)) {
            return "MegaY"
        }
        return "Form52"
    }
    if ($Variant -eq 11) {
        return "Alolan"
    }
    if ($Variant -eq 31) {
        return "Galarian"
    }
    if ($Form -eq 0 -and $Variant -eq 0) {
        return ""
    }
    return "Form${Form}_Variant${Variant}"
}

function Get-GenderLabel(
    [int]$SpeciesId,
    [int]$Form,
    [int]$Variant,
    [Collections.Generic.HashSet[string]]$AvailableStems
) {
    if ($Form -eq 1 -and $Variant -eq 0) {
        return "female"
    }
    if ($Form -eq 0 -and $Variant -eq 0) {
        $femaleStem = "pm{0:D4}_01_00" -f $SpeciesId
        if ($AvailableStems.Contains($femaleStem)) {
            return "male"
        }
        if ($SpeciesId -in @(120, 121, 137, 150)) {
            return "genderless"
        }
    }
    return "unisex"
}

function Find-PackageDirectory(
    [string]$Root,
    [int]$SpeciesId,
    [string]$ResourceStem
) {
    $prefix = "ik_pokemondatapm{0:D4}{1}" -f $SpeciesId, $ResourceStem
    $candidates = @(
        Get-ChildItem -LiteralPath $Root -Directory -Filter "${prefix}*.trpak" |
            Where-Object { $_.Name -notmatch "icon" } |
            ForEach-Object {
                [pscustomobject]@{
                    Name = $_.Name
                    FileCount = @(Get-ChildItem -LiteralPath $_.FullName -File).Count
                }
            } |
            Sort-Object @{Expression = "FileCount"; Descending = $true}, Name
    )
    if ($candidates.Count -eq 0) {
        throw "No extracted Z-A TRPAK package found for $ResourceStem."
    }
    return [string]$candidates[0].Name
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Resolve-FullPath (Join-Path $scriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($RecipePath)) {
    $RecipePath = Join-Path $scriptRoot "gamefreak_pokemon_imports_za.json"
}
$RecipePath = Resolve-FullPath $RecipePath
if ([string]::IsNullOrWhiteSpace($HashListPath)) {
    $HashListPath =
        "D:\ProjectData\Games\PokemonAutochess\Assets\pokemon-autochess\source\gamefreak\pokemon-legends-za\v2.0.0\hashes_inside_fd.txt"
}
$HashListPath = Resolve-FullPath $HashListPath
$PackageRoot = Resolve-FullPath $PackageRoot

foreach ($requiredPath in @($RecipePath, $HashListPath, $PackageRoot)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required Z-A inventory input is missing: $requiredPath"
    }
}

$recipe = Get-Content -LiteralPath $RecipePath -Raw | ConvertFrom-Json
if ([string]$recipe.schema -ne "phlosion-gamefreak-import-recipe-v1" -or
    [string]$recipe.sourceGame -ne "pokemon-legends-za-v2.0.0") {
    throw "The selected recipe is not the supported Z-A v2.0.0 recipe."
}

$speciesNames = @{}
Get-ChildItem -LiteralPath $scriptRoot -Filter "gamefreak_pokemon_imports*.json" -File |
    ForEach-Object {
        $candidateRecipe = Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
        foreach ($item in @($candidateRecipe.imports)) {
            $speciesId = [int]$item.speciesId
            if ($speciesId -ge 1 -and $speciesId -le 151 -and
                -not $speciesNames.ContainsKey($speciesId)) {
                $speciesNames[$speciesId] = [string]$item.speciesName
            }
        }
    }
$speciesNames[142] = "Aerodactyl"

$inventory = @{}
$pattern = [regex](
    "^0x[0-9A-Fa-f]{16}\s+ik_pokemon/data/pm" +
    "(?<species>\d{4})/(?<stem>pm\d{4}_(?<form>\d{2})_(?<variant>\d{2}))" +
    "/(?<file>[^/]+)$")
foreach ($line in [IO.File]::ReadLines($HashListPath)) {
    $match = $pattern.Match($line)
    if (-not $match.Success) {
        continue
    }
    $speciesId = [int]$match.Groups["species"].Value
    if ($speciesId -lt 1 -or $speciesId -gt 151) {
        continue
    }
    $stem = $match.Groups["stem"].Value
    if (-not $inventory.ContainsKey($stem)) {
        $inventory[$stem] = [ordered]@{
            SpeciesId = $speciesId
            Form = [int]$match.Groups["form"].Value
            Variant = [int]$match.Groups["variant"].Value
            Files = [Collections.Generic.HashSet[string]]::new(
                [StringComparer]::OrdinalIgnoreCase)
        }
    }
    [void]$inventory[$stem].Files.Add($match.Groups["file"].Value)
}

$availableStems = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
foreach ($stem in $inventory.Keys) {
    $files = $inventory[$stem].Files
    if ($files.Contains("$stem.trmdl") -and
        $files.Contains("$stem.trmtr") -and
        $files.Contains("${stem}_rare.trmtr")) {
        [void]$availableStems.Add($stem)
    }
}

$existingByStem = @{}
foreach ($item in @($recipe.imports)) {
    $existingByStem[[string]$item.resourceStem] = $item
}

$synchronized = [Collections.Generic.List[object]]::new()
$addedStems = [Collections.Generic.List[string]]::new()
foreach ($stem in @($availableStems | Sort-Object)) {
    if ($existingByStem.ContainsKey($stem)) {
        $synchronized.Add($existingByStem[$stem])
        continue
    }
    $source = $inventory[$stem]
    $speciesId = [int]$source.SpeciesId
    if (-not $speciesNames.ContainsKey($speciesId)) {
        throw "No canonical Kanto species name is available for #$speciesId ($stem)."
    }
    $form = [int]$source.Form
    $variant = [int]$source.Variant
    $speciesName = [string]$speciesNames[$speciesId]
    $suffix = Get-OutputSuffix $speciesId $form $variant
    $baseOutputStem = "{0:D4}_{1}_ZA" -f $speciesId, $speciesName
    if (-not [string]::IsNullOrWhiteSpace($suffix)) {
        $baseOutputStem += "_$suffix"
    }
    $entry = [ordered]@{
        speciesId = $speciesId
        speciesName = $speciesName
        form = $form
        gender = $variant
        genderLabel = Get-GenderLabel $speciesId $form $variant $availableStems
        resourceFolder = "pm{0:D4}/{1}" -f $speciesId, $stem
        resourceStem = $stem
        packageDirectory = Find-PackageDirectory $PackageRoot $speciesId $stem
        outputs = @(
            [ordered]@{ appearance = "regular"; stem = $baseOutputStem },
            [ordered]@{ appearance = "shiny"; stem = "${baseOutputStem}_Shiny" }
        )
    }
    $synchronized.Add([pscustomobject]$entry)
    $addedStems.Add($stem)
}

$synchronized = @(
    $synchronized |
        Sort-Object speciesId, form, gender
)
$recipe.imports = $synchronized
$json = $recipe | ConvertTo-Json -Depth 20
$currentJson = Get-Content -LiteralPath $RecipePath -Raw
$changed = $currentJson.TrimEnd() -ne $json.TrimEnd()

Write-Host (
    "Z-A Kanto recipe inventory: {0} species, {1} model forms, {2} outputs; {3} new forms." -f
    @($synchronized.speciesId | Sort-Object -Unique).Count,
    $synchronized.Count,
    @($synchronized.outputs).Count,
    $addedStems.Count)
if ($addedStems.Count -gt 0) {
    Write-Host ("New forms: " + ($addedStems -join ", "))
}

if ($Check) {
    if ($changed) {
        throw "The tracked Z-A Kanto import recipe is not synchronized with the source inventory."
    }
    Write-Host "The tracked recipe matches the available Z-A Kanto inventory."
    exit 0
}

if ($changed) {
    [IO.File]::WriteAllText(
        $RecipePath,
        $json + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))
    Write-Host "Updated $RecipePath"
} else {
    Write-Host "No recipe changes were required."
}
