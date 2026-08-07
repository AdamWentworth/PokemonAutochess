[CmdletBinding()]
param(
    [string]$RecipePath = "",
    [string]$RomFsRoot = "\\TNAS-98B9\pokemon\Game Files\Switch\Pokemon_Legends_Arceus_v1.1.1_Merged_RomFS",
    [string]$DepotRoot = $env:PHLOSION_ASSET_DEPOT,
    [string]$GfToolRoot = "D:\DevTools\ThirdParty\PokemonScarlet\gftool",
    [string]$SwitchToolboxRoot = "D:\DevTools\ThirdParty\Switch\Switch-Toolbox",
    [string]$OodleDecoder = $env:PHLOSION_OOZ_DECODER,
    [int[]]$SpeciesId = @(),
    [switch]$PlanOnly,
    [switch]$Force,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Resolve-FullPath([string]$PathValue) {
    return [IO.Path]::GetFullPath($PathValue)
}

function Assert-PathUnderRoot(
    [string]$PathValue,
    [string]$RootValue,
    [string]$Description) {
    $path = Resolve-FullPath $PathValue
    $root = (Resolve-FullPath $RootValue).TrimEnd('\', '/') +
        [IO.Path]::DirectorySeparatorChar
    if (-not $path.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description escapes its allowed root: $path (root: $root)"
    }
    return $path
}

function Resolve-RelativePath(
    [string]$RootValue,
    [string]$RelativePath,
    [string]$Description) {
    if ([IO.Path]::IsPathRooted($RelativePath)) {
        throw "$Description must be relative: $RelativePath"
    }
    return Assert-PathUnderRoot `
        (Join-Path $RootValue $RelativePath.Replace('/', [IO.Path]::DirectorySeparatorChar)) `
        $RootValue `
        $Description
}

function Get-Sha256([string]$PathValue) {
    return (Get-FileHash -LiteralPath $PathValue -Algorithm SHA256).Hash
}

function Publish-File(
    [string]$Source,
    [string]$Destination,
    [string]$AllowedRoot,
    [bool]$AllowReplace) {
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

function Publish-Directory(
    [string]$Source,
    [string]$Destination,
    [string]$AllowedRoot,
    [bool]$AllowReplace) {
    $destinationPath = Assert-PathUnderRoot $Destination $AllowedRoot "Staged source directory"
    if ((Test-Path -LiteralPath $destinationPath) -and -not $AllowReplace) {
        throw "Staged source directory exists; pass -Force to replace it: $destinationPath"
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $destinationPath) -Force | Out-Null
    $partial = "$destinationPath.partial.$([Guid]::NewGuid().ToString('N'))"
    $backup = "$destinationPath.backup.$([Guid]::NewGuid().ToString('N'))"
    Copy-Item -LiteralPath $Source -Destination $partial -Recurse -Force
    try {
        if (Test-Path -LiteralPath $destinationPath) {
            Move-Item -LiteralPath $destinationPath -Destination $backup
        }
        Move-Item -LiteralPath $partial -Destination $destinationPath
        if (Test-Path -LiteralPath $backup) {
            Assert-PathUnderRoot $backup $AllowedRoot "Staging backup" | Out-Null
            Remove-Item -LiteralPath $backup -Recurse -Force
        }
    } catch {
        if ((-not (Test-Path -LiteralPath $destinationPath)) -and
            (Test-Path -LiteralPath $backup)) {
            Move-Item -LiteralPath $backup -Destination $destinationPath
        }
        throw
    } finally {
        if (Test-Path -LiteralPath $partial) {
            Assert-PathUnderRoot $partial $AllowedRoot "Staging partial" | Out-Null
            Remove-Item -LiteralPath $partial -Recurse -Force
        }
    }
}

function Write-JsonAtomically(
    [object]$Document,
    [string]$Destination,
    [string]$AllowedRoot) {
    $destinationPath = Assert-PathUnderRoot $Destination $AllowedRoot "Generated catalog"
    New-Item -ItemType Directory -Path (Split-Path -Parent $destinationPath) -Force | Out-Null
    $partial = "$destinationPath.partial.$([Guid]::NewGuid().ToString('N'))"
    $Document | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $partial -Encoding UTF8
    if (Test-Path -LiteralPath $destinationPath) {
        Remove-Item -LiteralPath $destinationPath -Force
    }
    Move-Item -LiteralPath $partial -Destination $destinationPath
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($RecipePath)) {
    $RecipePath = Join-Path $scriptRoot "gamefreak_pokemon_imports_pla.json"
}
if ([string]::IsNullOrWhiteSpace($DepotRoot)) {
    $DepotRoot = "D:\ProjectData\Games\PokemonAutochess\Assets"
}
if ([string]::IsNullOrWhiteSpace($OodleDecoder)) {
    $OodleDecoder = "D:\DevTools\ThirdParty\Ooz\x64\Release\ooz.exe"
}

$RecipePath = Resolve-FullPath $RecipePath
$RomFsRoot = Resolve-FullPath $RomFsRoot
$DepotRoot = Resolve-FullPath $DepotRoot
$GfToolRoot = Resolve-FullPath $GfToolRoot
$SwitchToolboxRoot = Resolve-FullPath $SwitchToolboxRoot
$OodleDecoder = Resolve-FullPath $OodleDecoder
$recipe = Get-Content -LiteralPath $RecipePath -Raw | ConvertFrom-Json
if ($recipe.schema -ne "phlosion-gamefreak-import-recipe-v1") {
    throw "Unsupported import recipe schema: $($recipe.schema)"
}
if ($recipe.PSObject.Properties.Name -notcontains "packageRootRelativePath") {
    throw "Recipe must declare packageRootRelativePath for GFPAK staging."
}

$selected = @($recipe.imports)
if ($SpeciesId.Count -gt 0) {
    $selected = @($selected | Where-Object { $SpeciesId -contains [int]$_.speciesId })
}
if ($selected.Count -eq 0) {
    throw "Recipe/filter selected no Pokemon sources."
}

$projectDepot = Join-Path $DepotRoot "pokemon-autochess"
$sourceVersionRoot = Assert-PathUnderRoot `
    (Join-Path $projectDepot ("source\gamefreak\" + $recipe.sourceDepotFolder + "\" + $recipe.sourceVersion)) `
    $projectDepot `
    "Source version"
$packageRoot = Resolve-RelativePath $RomFsRoot $recipe.packageRootRelativePath "GFPAK root"
$resourceRoot = Resolve-RelativePath $sourceVersionRoot $recipe.resourceRootRelativePath "Resource root"
$catalogPath = Resolve-RelativePath $sourceVersionRoot $recipe.catalogRelativePath "Catalog"
$archiveDepotRoot = Resolve-RelativePath $sourceVersionRoot $recipe.packageRootRelativePath "Archive depot root"
$manifestRoot = Resolve-RelativePath $sourceVersionRoot "manifests/gfpak" "Extraction manifest root"
$hashEvidence = Join-Path $SwitchToolboxRoot "File_Format_Library\Resources\Hashes\Pkmn.txt"
$exporterProject = Join-Path $GfToolRoot "TrinityBatchExporter\TrinityBatchExporter.csproj"
$exporterDll = Join-Path $GfToolRoot "TrinityBatchExporter\bin\Release\net8.0-windows7.0\TrinityBatchExporter.dll"

foreach ($required in @($packageRoot, $hashEvidence, $OodleDecoder)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "GFPAK staging dependency is missing: $required"
    }
}

$jobs = @()
foreach ($item in $selected) {
    $packageStem = if ($item.PSObject.Properties.Name -contains "packageStem") {
        [string]$item.packageStem
    } else {
        "pm{0:D4}_{1:D2}_{2:D2}" -f [int]$item.speciesId, [int]$item.form, [int]$item.gender
    }
    if ($packageStem -notmatch '^pm[0-9]{4}_[0-9]{2}_[0-9]{2}$') {
        throw "Invalid Trinity GFPAK package stem: $packageStem"
    }
    $archive = Join-Path $packageRoot ($packageStem + ".gfpak")
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        throw "GFPAK source is missing: $archive"
    }
    $jobs += [pscustomobject]@{
        Item = $item
        PackageStem = $packageStem
        Archive = $archive
    }
}

Write-Host "Trinity GFPAK staging plan: $($jobs.Count) species"
foreach ($job in $jobs) {
    Write-Host ("  #{0:D4} {1} <- {2}.gfpak" -f
        [int]$job.Item.speciesId,
        $job.Item.speciesName,
        $job.PackageStem)
}
if ($PlanOnly) {
    Write-Host "Plan validated; no files were written."
    exit 0
}

if (-not $SkipBuild) {
    $env:DOTNET_ROLL_FORWARD = "Major"
    & dotnet build $exporterProject -c Release -v:minimal
    if ($LASTEXITCODE -ne 0) {
        throw "TrinityBatchExporter build failed with exit code $LASTEXITCODE"
    }
}
if (-not (Test-Path -LiteralPath $exporterDll -PathType Leaf)) {
    throw "TrinityBatchExporter not found: $exporterDll"
}

$catalogEntries = if (Test-Path -LiteralPath $catalogPath -PathType Leaf) {
    @((Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json).entries)
} else {
    @()
}
$tempParent = Resolve-FullPath ([IO.Path]::GetTempPath())
$tempRoot = Join-Path $tempParent ("phlosion-trinity-gfpak-stage-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tempRoot | Out-Null
try {
    foreach ($job in $jobs) {
        $stem = $job.PackageStem
        $jobRoot = Join-Path $tempRoot $stem
        $extractRoot = Join-Path $jobRoot "extract"
        $stageRoot = Join-Path $jobRoot "stage"
        $stageAnm = Join-Path $stageRoot "anm"
        $stageMdl = Join-Path $stageRoot "mdl"
        $stageLocators = Join-Path $stageRoot "locators"
        New-Item -ItemType Directory -Path $jobRoot -Force | Out-Null

        $sourceArchive = Join-Path $archiveDepotRoot ($stem + ".gfpak")
        Publish-File $job.Archive $sourceArchive $sourceVersionRoot ([bool]$Force)
        $extractManifest = Join-Path $jobRoot ($stem + ".gfpak.json")
        $env:DOTNET_ROLL_FORWARD = "Major"
        & dotnet $exporterDll `
            --gfpak $sourceArchive `
            --output $extractRoot `
            --manifest $extractManifest `
            --name-evidence $hashEvidence `
            --pokemon-stem $stem `
            --oodle-decoder $OodleDecoder `
            --source-game ([string]$recipe.sourceGame)
        if ($LASTEXITCODE -ne 0) {
            throw "GFPAK extraction failed for $stem"
        }

        $resolvedRoot = Join-Path $extractRoot "resolved"
        $resolvedFiles = @(Get-ChildItem -LiteralPath $resolvedRoot -File)
        foreach ($requiredName in @(
            "$stem.trmdl",
            "$stem.trmmt",
            "$stem.trpokecfg",
            "$stem.trmtr",
            "${stem}_rare.trmtr")) {
            if (-not (Test-Path -LiteralPath (Join-Path $resolvedRoot $requiredName) -PathType Leaf)) {
                throw "Resolved GFPAK resource is missing: $requiredName"
            }
        }

        foreach ($file in $resolvedFiles) {
            $extension = $file.Extension.ToLowerInvariant()
            $destinationRoot = if ($file.Name -eq "$stem.trpokecfg") {
                $stageRoot
            } elseif ($file.Name -eq "${stem}_eff.trskl") {
                $stageLocators
            } elseif ($extension -in @(
                '.tranm', '.tracm', '.traef', '.tracl', '.tracn',
                '.tracp', '.tracr', '.tracs', '.tralk')) {
                $stageAnm
            } else {
                $stageMdl
            }
            New-Item -ItemType Directory -Path $destinationRoot -Force | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $destinationRoot $file.Name) -Force
        }

        $speciesFolder = "pm{0:D4}" -f [int]$job.Item.speciesId
        $destination = Join-Path $resourceRoot (Join-Path $speciesFolder $stem)
        Publish-Directory $stageRoot $destination $resourceRoot ([bool]$Force)
        Publish-File `
            $extractManifest `
            (Join-Path $manifestRoot ($stem + ".gfpak.json")) `
            $sourceVersionRoot `
            ([bool]$Force)

        $species = [int]$job.Item.speciesId
        $form = [int]$job.Item.form
        $gender = [int]$job.Item.gender
        $catalogEntries = @($catalogEntries | Where-Object {
            [int]$_.species -ne $species -or
            [int]$_.form -ne $form -or
            [int]$_.gender -ne $gender
        })
        $folder = "$speciesFolder/$stem"
        $catalogEntries += [pscustomobject]@{
            species = $species
            form = $form
            gender = $gender
            resourceFolder = "$folder/anm"
            modelPath = "$folder/mdl/$stem.trmdl"
            materialTablePath = "$folder/mdl/$stem.trmmt"
            configPath = "$folder/$stem.trpokecfg"
            regularMaterialPaths = @("$folder/mdl/$stem.trmtr")
            rareMaterialPaths = @("$folder/mdl/${stem}_rare.trmtr")
            hasExplicitRareMaterial = $true
            variantStrategy = "shared-geometry-material-override"
            sourceArchive = ($sourceArchive.Substring($sourceVersionRoot.Length + 1)).Replace('\', '/')
        }
    }

    $catalogEntries = @($catalogEntries | Sort-Object species, form, gender)
    $catalog = [ordered]@{
        generatedUtc = [DateTimeOffset]::UtcNow.ToString("o")
        catalogVersion = 1
        catalogEntryCount = $catalogEntries.Count
        speciesIdCount = @($catalogEntries.species | Sort-Object -Unique).Count
        entriesWithExplicitRareMaterial = @($catalogEntries | Where-Object hasExplicitRareMaterial).Count
        entriesWithoutExplicitRareMaterial = @($catalogEntries | Where-Object { -not $_.hasExplicitRareMaterial }).Count
        entries = $catalogEntries
    }
    Write-JsonAtomically $catalog $catalogPath $sourceVersionRoot
    Write-Host "Staged $($jobs.Count) Trinity GFPAK species and updated $catalogPath"
} finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Assert-PathUnderRoot $tempRoot $tempParent "Staging temporary directory" | Out-Null
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
