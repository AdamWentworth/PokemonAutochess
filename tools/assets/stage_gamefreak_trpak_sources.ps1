[CmdletBinding()]
param(
    [string]$RecipePath = "",
    [string]$GameFilesRoot = "\\TNAS-98B9\pokemon\Game Files\Switch\Pokemon_Legends_ZA_v2.0.0_Merged_GameFiles",
    [string]$DepotRoot = $env:PHLOSION_ASSET_DEPOT,
    [string]$HashListPath = "",
    [int[]]$SpeciesId = @(),
    [switch]$PlanOnly,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Resolve-FullPath([string]$PathValue) {
    return [IO.Path]::GetFullPath($PathValue)
}

function Assert-PathUnderRoot([string]$PathValue, [string]$RootValue, [string]$Description) {
    $path = Resolve-FullPath $PathValue
    $root = (Resolve-FullPath $RootValue).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $path.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description escapes its allowed root: $path (root: $root)"
    }
    return $path
}

function Resolve-RelativePath([string]$RootValue, [string]$RelativePath, [string]$Description) {
    if ([IO.Path]::IsPathRooted($RelativePath)) {
        throw "$Description must be relative: $RelativePath"
    }
    $native = $RelativePath.Replace('/', [IO.Path]::DirectorySeparatorChar)
    return Assert-PathUnderRoot (Join-Path $RootValue $native) $RootValue $Description
}

function Get-Sha256([string]$PathValue) {
    return (Get-FileHash -LiteralPath $PathValue -Algorithm SHA256).Hash
}

function Publish-File([string]$Source, [string]$Destination, [string]$AllowedRoot, [bool]$AllowReplace) {
    $destinationPath = Assert-PathUnderRoot $Destination $AllowedRoot "Staged source file"
    if (Test-Path -LiteralPath $destinationPath -PathType Leaf) {
        if ((Get-Sha256 $Source) -eq (Get-Sha256 $destinationPath)) {
            return
        }
        if (-not $AllowReplace) {
            throw "Staged source differs; pass -Force to replace it: $destinationPath"
        }
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $destinationPath) -Force | Out-Null
    $partial = "$destinationPath.partial.$([Guid]::NewGuid().ToString('N'))"
    Copy-Item -LiteralPath $Source -Destination $partial -Force
    if (Test-Path -LiteralPath $destinationPath) {
        Remove-Item -LiteralPath $destinationPath -Force
    }
    Move-Item -LiteralPath $partial -Destination $destinationPath
}

function Write-JsonAtomically([object]$Document, [string]$Destination, [string]$AllowedRoot) {
    $destinationPath = Assert-PathUnderRoot $Destination $AllowedRoot "Generated manifest"
    New-Item -ItemType Directory -Path (Split-Path -Parent $destinationPath) -Force | Out-Null
    $partial = "$destinationPath.partial.$([Guid]::NewGuid().ToString('N'))"
    $Document | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $partial -Encoding UTF8
    if (Test-Path -LiteralPath $destinationPath) {
        Remove-Item -LiteralPath $destinationPath -Force
    }
    Move-Item -LiteralPath $partial -Destination $destinationPath
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($RecipePath)) {
    $RecipePath = Join-Path $scriptRoot "gamefreak_pokemon_imports_za.json"
}
$RecipePath = Resolve-FullPath $RecipePath
$GameFilesRoot = Resolve-FullPath $GameFilesRoot
if ([string]::IsNullOrWhiteSpace($DepotRoot)) {
    $DepotRoot = "D:\ProjectData\Games\PokemonAutochess\Assets"
}
$DepotRoot = Resolve-FullPath $DepotRoot
$projectDepot = Join-Path $DepotRoot "pokemon-autochess"

$recipe = Get-Content -LiteralPath $RecipePath -Raw | ConvertFrom-Json
if ($recipe.schema -ne "phlosion-gamefreak-import-recipe-v1") {
    throw "Unsupported import recipe schema: $($recipe.schema)"
}
$sourceDepotFolder = [string]$recipe.sourceDepotFolder
if ([string]::IsNullOrWhiteSpace($sourceDepotFolder) -or
    $sourceDepotFolder.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
    throw "Invalid sourceDepotFolder: $sourceDepotFolder"
}
$sourceVersionRoot = Assert-PathUnderRoot (
    Join-Path $projectDepot ("source\gamefreak\" + $sourceDepotFolder + "\" + $recipe.sourceVersion)
) $projectDepot "Source version"
$packageRoot = Resolve-RelativePath $GameFilesRoot ([string]$recipe.packageRootRelativePath) "Package root"
$rehydratedRoot = Resolve-RelativePath $sourceVersionRoot "rehydrated" "Rehydrated source root"
$catalogPath = Resolve-RelativePath $sourceVersionRoot ([string]$recipe.catalogRelativePath) "Catalog"
$resourceRoot = Resolve-RelativePath $sourceVersionRoot ([string]$recipe.resourceRootRelativePath) "Resource root"

if ([string]::IsNullOrWhiteSpace($HashListPath)) {
    $HashListPath = Resolve-RelativePath $sourceVersionRoot ([string]$recipe.hashListRelativePath) "Hash list"
} else {
    $HashListPath = Resolve-FullPath $HashListPath
}
if (-not (Test-Path -LiteralPath $HashListPath -PathType Leaf)) {
    throw "Z-A hash list is missing: $HashListPath"
}
$hashListSha = Get-Sha256 $HashListPath
if ($recipe.PSObject.Properties.Name -contains "hashListSha256" -and
    $hashListSha -ne [string]$recipe.hashListSha256) {
    throw "Hash-list identity mismatch: expected $($recipe.hashListSha256), got $hashListSha"
}

$selected = @($recipe.imports)
if ($SpeciesId.Count -gt 0) {
    $selected = @($selected | Where-Object { $SpeciesId -contains [int]$_.speciesId })
}
if ($selected.Count -eq 0) {
    throw "Recipe/filter selected no Pokemon sources."
}

$packages = @($selected | ForEach-Object { [string]$_.packageDirectory })
$packages += @($recipe.sharedPackageDirectories | ForEach-Object { [string]$_ })
$packages = @($packages | Sort-Object -Unique)
foreach ($package in $packages) {
    $packagePath = Resolve-RelativePath $packageRoot $package "TRPAK package"
    if (-not (Test-Path -LiteralPath $packagePath -PathType Container)) {
        throw "TRPAK package directory is missing: $packagePath"
    }
}

$hashNames = @{}
foreach ($line in [IO.File]::ReadLines($HashListPath)) {
    if ($line -match '^0x([0-9A-Fa-f]{16})\s+(.+)$') {
        $hashNames[$matches[1].ToUpperInvariant()] = $matches[2]
    }
}
if ($hashNames.Count -eq 0) {
    throw "Hash list contains no usable entries: $HashListPath"
}

$resolvedEntries = New-Object System.Collections.Generic.List[object]
$resolvedDestinations = @{}
foreach ($package in $packages) {
    $packagePath = Resolve-RelativePath $packageRoot $package "TRPAK package"
    foreach ($file in Get-ChildItem -LiteralPath $packagePath -File) {
        if ($file.Name -notmatch '- ([0-9A-Fa-f]{16})\.') {
            throw "Unrecognized extracted TRPAK filename: $($file.FullName)"
        }
        $fileHash = $matches[1].ToUpperInvariant()
        if (-not $hashNames.ContainsKey($fileHash)) {
            throw "Hash 0x$fileHash is absent from $HashListPath"
        }
        $resolvedName = [string]$hashNames[$fileHash]
        $destination = Resolve-RelativePath $rehydratedRoot $resolvedName "Resolved TRPAK resource"
        if ($resolvedDestinations.ContainsKey($destination)) {
            $prior = [string]$resolvedDestinations[$destination]
            if ((Get-Sha256 $prior) -ne (Get-Sha256 $file.FullName)) {
                throw "Two packages resolve different payloads to $destination"
            }
            continue
        }
        $resolvedDestinations[$destination] = $file.FullName
        $resolvedEntries.Add([pscustomobject]@{
            package = $package
            source_file = $file.Name
            file_hash = "0x$fileHash"
            resolved_path = $resolvedName.Replace('\', '/')
            byte_length = [long]$file.Length
            sha256 = Get-Sha256 $file.FullName
            source_path = $file.FullName
            destination = $destination
        })
    }
}

Write-Host "Z-A TRPAK source plan: $($selected.Count) species, $($packages.Count) packages, $($resolvedEntries.Count) resolved files"
foreach ($item in $selected) {
    Write-Host ("  #{0:D4} {1} <- {2}" -f [int]$item.speciesId, $item.speciesName, $item.packageDirectory)
}
if ($PlanOnly) {
    Write-Host "Plan validated; no files were written."
    exit 0
}

foreach ($entry in $resolvedEntries) {
    Publish-File $entry.source_path $entry.destination $rehydratedRoot ([bool]$Force)
}

$catalogEntries = @()
if (Test-Path -LiteralPath $catalogPath -PathType Leaf) {
    $catalogEntries = @((Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json).entries)
}
foreach ($item in $selected) {
    $species = [int]$item.speciesId
    $form = [int]$item.form
    $gender = [int]$item.gender
    $catalogEntries = @($catalogEntries | Where-Object {
        [int]$_.species -ne $species -or [int]$_.form -ne $form -or [int]$_.gender -ne $gender
    })
    $folder = ([string]$item.resourceFolder).Replace('\', '/')
    $stem = [string]$item.resourceStem
    $modelRelative = "$folder/$stem.trmdl"
    $regularMaterial = "$folder/$stem.trmtr"
    $rareMaterial = "$folder/${stem}_rare.trmtr"
    foreach ($required in @($modelRelative, $regularMaterial, $rareMaterial)) {
        $requiredPath = Resolve-RelativePath $resourceRoot $required "Catalog resource"
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "Rehydrated catalog resource is missing: $requiredPath"
        }
    }
    $catalogEntries += [pscustomobject]@{
        species = $species
        form = $form
        gender = $gender
        resourceFolder = $folder
        modelPath = $modelRelative
        materialTablePath = "$folder/$stem.trmmt"
        configPath = "$folder/$stem.trpokecfg"
        regularMaterialPaths = @($regularMaterial)
        rareMaterialPaths = @($rareMaterial)
        hasExplicitRareMaterial = $true
        variantStrategy = "shared-geometry-material-override"
        sourcePackageDirectory = [string]$item.packageDirectory
    }
}
$catalogEntries = @($catalogEntries | Sort-Object species, form, gender)
$catalog = [ordered]@{
    generatedUtc = [DateTimeOffset]::UtcNow.ToString("o")
    sourceGame = [string]$recipe.sourceGame
    catalogVersion = 1
    catalogEntryCount = $catalogEntries.Count
    speciesIdCount = @($catalogEntries.species | Sort-Object -Unique).Count
    entriesWithExplicitRareMaterial = @($catalogEntries | Where-Object hasExplicitRareMaterial).Count
    entriesWithoutExplicitRareMaterial = @($catalogEntries | Where-Object { -not $_.hasExplicitRareMaterial }).Count
    entries = $catalogEntries
}
Write-JsonAtomically $catalog $catalogPath $sourceVersionRoot

$manifestPath = Resolve-RelativePath $sourceVersionRoot "manifests/trpak-stage-manifest.json" "Stage manifest"
$manifestEntries = @($resolvedEntries | ForEach-Object {
    [ordered]@{
        package = $_.package
        source_file = $_.source_file
        file_hash = $_.file_hash
        resolved_path = $_.resolved_path
        byte_length = $_.byte_length
        sha256 = $_.sha256
    }
})
$manifest = [ordered]@{
    schema = "phlosion-trpak-source-stage-manifest-v1"
    generated_utc = [DateTimeOffset]::UtcNow.ToString("o")
    source_game = [string]$recipe.sourceGame
    source_game_files_root = $GameFilesRoot
    hash_list = $HashListPath
    hash_list_sha256 = $hashListSha
    package_count = $packages.Count
    resolved_file_count = $resolvedEntries.Count
    packages = $packages
    entries = $manifestEntries
}
Write-JsonAtomically $manifest $manifestPath $sourceVersionRoot
Write-Host "Rehydrated and cataloged $($resolvedEntries.Count) native resources."
Write-Host "Catalog: $catalogPath"
Write-Host "Manifest: $manifestPath"
