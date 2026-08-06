[CmdletBinding()]
param(
    [string]$RecipePath = "",
    [string]$DepotRoot = $env:PHLOSION_ASSET_DEPOT,
    [string]$GameRoot = "",
    [string]$GfToolRoot = $env:PHLOSION_GFTOOL_ROOT,
    [int[]]$SpeciesId = @(),
    [switch]$PlanOnly,
    [switch]$Force,
    [switch]$SkipBuild,
    [switch]$SkipPublish,
    [switch]$Cook
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

function Resolve-CatalogPath([string]$RootValue, [string]$RelativePath) {
    $nativeRelative = $RelativePath.Replace('/', [IO.Path]::DirectorySeparatorChar)
    return Assert-PathUnderRoot (Join-Path $RootValue $nativeRelative) $RootValue "Catalog resource"
}

function Get-RelativePathUnderRoot([string]$RootValue, [string]$PathValue) {
    $path = Assert-PathUnderRoot $PathValue $RootValue "Relative-path input"
    $root = (Resolve-FullPath $RootValue).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    return $path.Substring($root.Length)
}

function Copy-DirectoryContents([string]$Source, [string]$Destination) {
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    foreach ($child in Get-ChildItem -LiteralPath $Source -Force) {
        Copy-Item -LiteralPath $child.FullName -Destination $Destination -Recurse -Force
    }
}

function Publish-File([string]$Source, [string]$Destination, [string]$AllowedRoot) {
    $destinationPath = Assert-PathUnderRoot $Destination $AllowedRoot "Published file"
    $parent = Split-Path -Parent $destinationPath
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $partial = "$destinationPath.partial.$([Guid]::NewGuid().ToString('N'))"
    Copy-Item -LiteralPath $Source -Destination $partial -Force
    if (Test-Path -LiteralPath $destinationPath) {
        Remove-Item -LiteralPath $destinationPath -Force
    }
    Move-Item -LiteralPath $partial -Destination $destinationPath
}

function Publish-Directory([string]$Source, [string]$Destination, [string]$AllowedRoot) {
    $destinationPath = Assert-PathUnderRoot $Destination $AllowedRoot "Published directory"
    $parent = Split-Path -Parent $destinationPath
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $partial = "$destinationPath.partial.$([Guid]::NewGuid().ToString('N'))"
    $backup = "$destinationPath.backup.$([Guid]::NewGuid().ToString('N'))"
    Copy-Item -LiteralPath $Source -Destination $partial -Recurse -Force
    try {
        if (Test-Path -LiteralPath $destinationPath) {
            Move-Item -LiteralPath $destinationPath -Destination $backup
        }
        Move-Item -LiteralPath $partial -Destination $destinationPath
        if (Test-Path -LiteralPath $backup) {
            Assert-PathUnderRoot $backup $AllowedRoot "Publish backup" | Out-Null
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
            Assert-PathUnderRoot $partial $AllowedRoot "Publish partial" | Out-Null
            Remove-Item -LiteralPath $partial -Recurse -Force
        }
    }
}

function Validate-PhModel([string]$PathValue, [string]$ExpectedVariant) {
    $document = Get-Content -LiteralPath $PathValue -Raw | ConvertFrom-Json
    if ($document.schema -ne "phlosion-native-model-ir-v1") {
        throw "Unexpected PHMODEL schema in $PathValue"
    }
    if ($document.source.material_variant -ne $ExpectedVariant) {
        throw "PHMODEL variant mismatch in $PathValue"
    }
    if ($ExpectedVariant -eq "shiny" -and
        ([string]::IsNullOrWhiteSpace([string]$document.source.material_source) -or
         [string]::IsNullOrWhiteSpace([string]$document.source.material_source_sha256))) {
        throw "Shiny PHMODEL is missing material provenance in $PathValue"
    }
    if ([int]$document.model.vertex_count -le 0 -or
        [int]$document.model.index_count -le 0 -or
        [int]$document.model.submesh_count -le 0 -or
        @($document.skeleton.bones).Count -le 0 -or
        @($document.animations).Count -le 0 -or
        @($document.materials).Count -le 0) {
        throw "PHMODEL structural validation failed: $PathValue"
    }
    return $document
}

function Set-AnimsetAirLocomotion([string]$PathValue, [object]$RoleConfig) {
    $document = Get-Content -LiteralPath $PathValue -Raw | ConvertFrom-Json
    if ($document.PSObject.Properties.Name -notcontains 'roles' -or
        $null -eq $document.roles) {
        $document | Add-Member -NotePropertyName 'roles' -NotePropertyValue ([pscustomobject]@{}) -Force
    }
    foreach ($role in @('move', 'air_idle', 'takeoff', 'land_a', 'land_b', 'land_c')) {
        if ($RoleConfig.PSObject.Properties.Name -notcontains $role) {
            throw "Air-locomotion recipe is missing '$role' for $PathValue"
        }
        $clipName = [string]$RoleConfig.$role
        $matches = @($document.clips | Where-Object { [string]$_.gltf_name -eq $clipName })
        if ($matches.Count -ne 1) {
            throw "Air-locomotion role '$role' must resolve exactly once to '$clipName' in $PathValue"
        }
        $document.roles | Add-Member -NotePropertyName $role -NotePropertyValue $clipName -Force
    }
    if ($document.PSObject.Properties.Name -notcontains 'meta' -or
        $null -eq $document.meta) {
        $document | Add-Member -NotePropertyName 'meta' -NotePropertyValue ([pscustomobject]@{}) -Force
    }
    $document.meta | Add-Member -NotePropertyName 'movementMode' -NotePropertyValue 'airborne' -Force

    $partial = "$PathValue.partial.$([Guid]::NewGuid().ToString('N'))"
    try {
        $document | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $partial -Encoding UTF8
        Move-Item -LiteralPath $partial -Destination $PathValue -Force
    } finally {
        if (Test-Path -LiteralPath $partial) {
            Remove-Item -LiteralPath $partial -Force
        }
    }
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $scriptRoot "..\.."
}
$GameRoot = Resolve-FullPath $GameRoot

if ([string]::IsNullOrWhiteSpace($RecipePath)) {
    $RecipePath = Join-Path $scriptRoot "gamefreak_pokemon_imports.json"
}
$RecipePath = Resolve-FullPath $RecipePath

if ([string]::IsNullOrWhiteSpace($DepotRoot)) {
    throw "PHLOSION_ASSET_DEPOT (or -DepotRoot) must identify the private asset depot."
}
$DepotRoot = Resolve-FullPath $DepotRoot
$projectDepot = Join-Path $DepotRoot "pokemon-autochess"

if ([string]::IsNullOrWhiteSpace($GfToolRoot)) {
    $GfToolRoot = "D:\DevTools\ThirdParty\PokemonScarlet\gftool"
}
$GfToolRoot = Resolve-FullPath $GfToolRoot

if (-not (Test-Path -LiteralPath $RecipePath -PathType Leaf)) {
    throw "Import recipe not found: $RecipePath"
}
$recipe = Get-Content -LiteralPath $RecipePath -Raw | ConvertFrom-Json
if ($recipe.schema -ne "phlosion-gamefreak-import-recipe-v1") {
    throw "Unsupported import recipe schema: $($recipe.schema)"
}

$sourceDepotFolder = if ($recipe.PSObject.Properties.Name -contains "sourceDepotFolder") {
    [string]$recipe.sourceDepotFolder
} else {
    "pokemon-scarlet"
}
if ([string]::IsNullOrWhiteSpace($sourceDepotFolder) -or
    $sourceDepotFolder.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
    throw "Invalid sourceDepotFolder: $sourceDepotFolder"
}
$sourceVersionRoot = Join-Path $projectDepot ("source\gamefreak\" + $sourceDepotFolder + "\" + $recipe.sourceVersion)
$sourceVersionRoot = Assert-PathUnderRoot $sourceVersionRoot $projectDepot "Source version"
$catalogPath = Resolve-CatalogPath $sourceVersionRoot $recipe.catalogRelativePath
$resourceRoot = Resolve-CatalogPath $sourceVersionRoot $recipe.resourceRootRelativePath
$resourceGraphRoot = if ($recipe.PSObject.Properties.Name -contains "resourceGraphRootRelativePath") {
    Resolve-CatalogPath $sourceVersionRoot $recipe.resourceGraphRootRelativePath
} else {
    $null
}
if (-not (Test-Path -LiteralPath $catalogPath -PathType Leaf)) {
    throw "Pokemon variant catalog not found: $catalogPath"
}
if (-not (Test-Path -LiteralPath $resourceRoot -PathType Container)) {
    throw "Pokemon resource root not found: $resourceRoot"
}
$catalog = Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json

$exporterProject = Join-Path $GfToolRoot "TrinityBatchExporter\TrinityBatchExporter.csproj"
$exporterDll = Join-Path $GfToolRoot "TrinityBatchExporter\bin\Release\net8.0-windows7.0\TrinityBatchExporter.dll"
if (-not $SkipBuild -and -not $PlanOnly) {
    $env:DOTNET_ROLL_FORWARD = "Major"
    & dotnet build $exporterProject -c Release -v:minimal
    if ($LASTEXITCODE -ne 0) {
        throw "TrinityBatchExporter build failed with exit code $LASTEXITCODE"
    }
}
if (-not $PlanOnly -and -not (Test-Path -LiteralPath $exporterDll -PathType Leaf)) {
    throw "TrinityBatchExporter not found: $exporterDll"
}

$selectedImports = @($recipe.imports)
if ($SpeciesId.Count -gt 0) {
    $selectedImports = @($selectedImports | Where-Object { $SpeciesId -contains [int]$_.speciesId })
}
if ($selectedImports.Count -eq 0) {
    throw "The recipe/filter selected no Pokemon imports."
}

$planned = New-Object System.Collections.Generic.List[object]
foreach ($item in $selectedImports) {
    $matches = @($catalog.entries | Where-Object {
        [int]$_.species -eq [int]$item.speciesId -and
        [int]$_.form -eq [int]$item.form -and
        [int]$_.gender -eq [int]$item.gender
    })
    if ($matches.Count -ne 1) {
        throw "Expected one catalog entry for species=$($item.speciesId) form=$($item.form) gender=$($item.gender); found $($matches.Count)."
    }
    $entry = $matches[0]
    $modelPath = Resolve-CatalogPath $resourceRoot $entry.modelPath
    $resourceFolder = Resolve-CatalogPath $resourceRoot $entry.resourceFolder
    foreach ($output in @($item.outputs)) {
        $appearance = [string]$output.appearance
        if ($appearance -ne "regular" -and $appearance -ne "shiny") {
            throw "Unsupported appearance '$appearance' for $($item.speciesName)."
        }
        if ($appearance -eq "shiny" -and -not [bool]$entry.hasExplicitRareMaterial) {
            throw "No explicit rare material exists for $($item.speciesName) ($($item.genderLabel))."
        }
        $planned.Add([pscustomobject]@{
            Item = $item
            Entry = $entry
            Appearance = $appearance
            Stem = [string]$output.stem
            ModelPath = $modelPath
            ResourceFolder = $resourceFolder
        })
    }
}

Write-Host "Game Freak import plan: $($planned.Count) canonical variants"
foreach ($job in $planned) {
    Write-Host ("  #{0:D4} {1} {2}/{3} -> {4}" -f
        [int]$job.Item.speciesId,
        $job.Item.speciesName,
        $job.Item.genderLabel,
        $job.Appearance,
        $job.Stem)
}
if ($PlanOnly) {
    Write-Host "Plan validated; no files were written."
    exit 0
}

$derivedImportRoot = Join-Path $projectDepot ("derived\imports\gamefreak\" + $recipe.sourceGame)
$depotModelsRoot = Join-Path $projectDepot "runtime\assets\models"
$depotObjectsRoot = Join-Path $projectDepot "runtime\content\phlosion\objects"
$gameModelsRoot = Join-Path $GameRoot "assets\models"
$gameObjectsRoot = Join-Path $GameRoot "content\phlosion\objects"
$forge = Join-Path $GameRoot "build\Debug\PhlosionForge.exe"
if ($Cook -and -not (Test-Path -LiteralPath $forge -PathType Leaf)) {
    throw "PhlosionForge is required for -Cook: $forge"
}

$tempParent = Resolve-FullPath ([IO.Path]::GetTempPath())
$tempRoot = Join-Path $tempParent ("phlosion-gamefreak-import-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tempRoot | Out-Null
$reportEntries = New-Object System.Collections.Generic.List[object]
try {
    foreach ($job in $planned) {
        Write-Host "Importing $($job.Stem)..."
        $jobRoot = Join-Path $tempRoot $job.Stem
        New-Item -ItemType Directory -Path $jobRoot | Out-Null
        $loadFolder = $job.ResourceFolder
        $loadModel = $job.ModelPath
        $materialSource = ""
        $rareMaterialPaths = @()

        if ($job.Appearance -eq "shiny") {
            # A form/gender folder may reference shared resources in a sibling
            # folder (Venusaur female does this). Preserve the species-level
            # relative graph instead of flattening one resource folder.
            $speciesSourceRoot = Split-Path -Parent $job.ResourceFolder
            $stagingSourceRoot = if ($null -ne $resourceGraphRoot) {
                $resourceGraphRoot
            } else {
                $speciesSourceRoot
            }
            $stageSourceRoot = Join-Path $jobRoot ([IO.Path]::GetFileName($stagingSourceRoot))
            Copy-DirectoryContents $stagingSourceRoot $stageSourceRoot
            $resourceRelative = Get-RelativePathUnderRoot $stagingSourceRoot $job.ResourceFolder
            $modelRelative = Get-RelativePathUnderRoot $stagingSourceRoot $job.ModelPath
            $loadFolder = Join-Path $stageSourceRoot $resourceRelative
            $loadModel = Join-Path $stageSourceRoot $modelRelative
            $rareMaterialPaths = @($job.Entry.rareMaterialPaths | ForEach-Object {
                Resolve-CatalogPath $resourceRoot $_
            })
            foreach ($rarePath in $rareMaterialPaths) {
                $rareName = [IO.Path]::GetFileName($rarePath)
                $targetName = $rareName -replace '_rare\.trmtr$', '.trmtr'
                if ($targetName -eq $rareName) {
                    throw "Rare material does not follow the _rare.trmtr convention: $rarePath"
                }
                $rareRelative = Get-RelativePathUnderRoot $stagingSourceRoot $rarePath
                $stageRarePath = Join-Path $stageSourceRoot $rareRelative
                $targetPath = Join-Path (Split-Path -Parent $stageRarePath) $targetName
                if (-not (Test-Path -LiteralPath $targetPath -PathType Leaf)) {
                    throw "Rare material target does not exist in staged dependencies: $targetPath"
                }
                Copy-Item -LiteralPath $stageRarePath -Destination $targetPath -Force
            }
            $materialSource = $rareMaterialPaths[0]
        }

        $exportRoot = Join-Path $jobRoot "export"
        $outputModel = Join-Path $exportRoot ($job.Stem + ".phmodel")
        $outputAnimset = Join-Path $exportRoot ($job.Stem + ".animset.json")
        $outputManifest = Join-Path $exportRoot "import-manifest.json"
        $arguments = @(
            $exporterDll,
            "--format", "native-ir",
            "--source-game", [string]$recipe.sourceGame,
            "--material-variant", $job.Appearance,
            "--source-model-identity", $job.ModelPath,
            "--model", $loadModel,
            "--animations", $loadFolder,
            "--output", $outputModel,
            "--animset", $outputAnimset,
            "--manifest", $outputManifest
        )
        if (-not [string]::IsNullOrWhiteSpace($materialSource)) {
            $arguments += @("--material-source", $materialSource)
        }
        $env:DOTNET_ROLL_FORWARD = "Major"
        & dotnet @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Native import failed for $($job.Stem) with exit code $LASTEXITCODE"
        }

        if ($job.Item.PSObject.Properties.Name -contains 'airLocomotion') {
            Set-AnimsetAirLocomotion $outputAnimset $job.Item.airLocomotion
        }

        $document = Validate-PhModel $outputModel $job.Appearance
        $payloadPath = Join-Path $exportRoot ($job.Stem + ".bin")
        $texturePath = Join-Path $exportRoot ($job.Stem + "_textures")
        foreach ($requiredPath in @($payloadPath, $outputAnimset, $outputManifest, $texturePath)) {
            if (-not (Test-Path -LiteralPath $requiredPath)) {
                throw "Importer omitted required output: $requiredPath"
            }
        }

        $canonicalPath = Join-Path $derivedImportRoot ($job.Stem + "_native-ir")
        if ((Test-Path -LiteralPath $canonicalPath) -and -not $Force) {
            throw "Canonical import already exists; pass -Force to replace it: $canonicalPath"
        }
        Publish-Directory $exportRoot $canonicalPath $derivedImportRoot

        if (-not $SkipPublish) {
            foreach ($destinationRoot in @($depotModelsRoot, $gameModelsRoot)) {
                Publish-File $outputModel (Join-Path $destinationRoot ($job.Stem + ".phmodel")) $destinationRoot
                Publish-File $payloadPath (Join-Path $destinationRoot ($job.Stem + ".bin")) $destinationRoot
                Publish-File $outputAnimset (Join-Path $destinationRoot ($job.Stem + ".animset.json")) $destinationRoot
                Publish-Directory $texturePath (Join-Path $destinationRoot ($job.Stem + "_textures")) $destinationRoot
            }
        }

        if ($Cook) {
            $relativeModel = "assets/models/$($job.Stem).phmodel"
            $cookOutput = @(& $forge cook-model $relativeModel 2>&1)
            $cookExitCode = $LASTEXITCODE
            foreach ($line in $cookOutput) {
                Write-Host ([string]$line)
            }
            if ($cookExitCode -ne 0) {
                throw "PhlosionForge failed to cook $($job.Stem) with exit code $cookExitCode"
            }

            $cookedObjectNames = @(
                @(
                    foreach ($line in $cookOutput) {
                        $text = [string]$line
                        if ($text -match 'content[\\/]phlosion[\\/]objects[\\/]([^\\/]+)[\\/]') {
                            $Matches[1]
                        }
                    }
                ) | Select-Object -Unique
            )
            if ($cookedObjectNames.Count -ne 1) {
                throw "Could not identify exactly one cooked object for $($job.Stem) from PhlosionForge output."
            }

            $cookedObjectName = [string]$cookedObjectNames[0]
            $cookedObjectPath = Assert-PathUnderRoot `
                (Join-Path $gameObjectsRoot $cookedObjectName) `
                $gameObjectsRoot `
                "Cooked object"
            if (-not (Test-Path -LiteralPath $cookedObjectPath -PathType Container)) {
                throw "PhlosionForge reported a cooked object that does not exist: $cookedObjectPath"
            }
            if (-not $SkipPublish) {
                Publish-Directory `
                    $cookedObjectPath `
                    (Join-Path $depotObjectsRoot $cookedObjectName) `
                    $depotObjectsRoot
            }
        }

        $reportEntries.Add([pscustomobject]@{
            species_id = [int]$job.Item.speciesId
            species_name = [string]$job.Item.speciesName
            form = [int]$job.Item.form
            gender = [int]$job.Item.gender
            gender_label = [string]$job.Item.genderLabel
            appearance = $job.Appearance
            stem = $job.Stem
            source_model = $job.ModelPath
            source_materials = @($rareMaterialPaths)
            vertex_count = [int]$document.model.vertex_count
            index_count = [int]$document.model.index_count
            submesh_count = [int]$document.model.submesh_count
            material_count = @($document.materials).Count
            bone_count = @($document.skeleton.bones).Count
            animation_count = @($document.animations).Count
            canonical_path = $canonicalPath
        })
    }

    $report = [ordered]@{
        schema = "phlosion-gamefreak-import-report-v1"
        generated_utc = [DateTimeOffset]::UtcNow.ToString("o")
        source_game = [string]$recipe.sourceGame
        recipe = $RecipePath
        catalog = $catalogPath
        import_count = $reportEntries.Count
        imports = @($reportEntries | ForEach-Object { $_ })
    }
    $reportPath = Join-Path $derivedImportRoot "latest-import-report.json"
    New-Item -ItemType Directory -Path $derivedImportRoot -Force | Out-Null
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
    Write-Host "Imported and validated $($reportEntries.Count) canonical variants."
    Write-Host "Report: $reportPath"
} finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Assert-PathUnderRoot $tempRoot $tempParent "Importer temporary directory" | Out-Null
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
