[CmdletBinding()]
param(
    [string]$RecipePath = "",
    [string]$RomFsRoot = "\\TNAS-98B9\pokemon\Game Files\Switch\Pokemon_Lets_Go_Pikachu_v0_RomFS",
    [string]$DepotRoot = $env:PHLOSION_ASSET_DEPOT,
    [string]$GameRoot = "",
    [string]$GfToolRoot = "D:\DevTools\ThirdParty\PokemonScarlet\gftool",
    [string]$SwitchToolboxRoot = "D:\DevTools\ThirdParty\Switch\Switch-Toolbox",
    [string]$SwitchToolboxRuntime = "D:\DevTools\ThirdParty\PokemonEnvironment\Switch-Toolbox-Final",
    [int[]]$SpeciesId = @(),
    [switch]$PlanOnly,
    [switch]$Force,
    [switch]$SkipBuild,
    [switch]$SkipPublish,
    [switch]$Cook
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'NativeModelPayloadStore.psm1') -Force

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

function Publish-File([string]$Source, [string]$Destination, [string]$AllowedRoot) {
    $destinationPath = Assert-PathUnderRoot $Destination $AllowedRoot "Published file"
    New-Item -ItemType Directory -Path (Split-Path -Parent $destinationPath) -Force | Out-Null
    $partial = "$destinationPath.partial.$([Guid]::NewGuid().ToString('N'))"
    Copy-Item -LiteralPath $Source -Destination $partial -Force
    if (Test-Path -LiteralPath $destinationPath) { Remove-Item -LiteralPath $destinationPath -Force }
    Move-Item -LiteralPath $partial -Destination $destinationPath
}

function Publish-Directory([string]$Source, [string]$Destination, [string]$AllowedRoot) {
    $destinationPath = Assert-PathUnderRoot $Destination $AllowedRoot "Published directory"
    New-Item -ItemType Directory -Path (Split-Path -Parent $destinationPath) -Force | Out-Null
    $partial = "$destinationPath.partial.$([Guid]::NewGuid().ToString('N'))"
    $backup = "$destinationPath.backup.$([Guid]::NewGuid().ToString('N'))"
    Copy-Item -LiteralPath $Source -Destination $partial -Recurse -Force
    try {
        if (Test-Path -LiteralPath $destinationPath) { Move-Item -LiteralPath $destinationPath -Destination $backup }
        Move-Item -LiteralPath $partial -Destination $destinationPath
        if (Test-Path -LiteralPath $backup) {
            Assert-PathUnderRoot $backup $AllowedRoot "Publish backup" | Out-Null
            Remove-Item -LiteralPath $backup -Recurse -Force
        }
    } catch {
        if ((-not (Test-Path -LiteralPath $destinationPath)) -and (Test-Path -LiteralPath $backup)) {
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

function Get-AnimationCategory([string]$Name) {
    $lower = $Name.ToLowerInvariant()
    if ($lower.Contains('wait')) { return 'idle' }
    if ($lower.Contains('walk') -or $lower.Contains('run') -or $lower.Contains('move')) { return 'move' }
    if ($lower.Contains('ba20') -or $lower.Contains('buturi') -or $lower.Contains('ba21') -or $lower.Contains('tokusyu')) { return 'attack' }
    if ($lower.Contains('damage')) { return 'hit' }
    if ($lower.Contains('down')) { return 'status' }
    return 'misc'
}

function Select-AnimationName([object[]]$Animations, [string[]]$Patterns) {
    foreach ($pattern in $Patterns) {
        $match = @($Animations | Where-Object { ([string]$_.name).IndexOf($pattern, [StringComparison]::OrdinalIgnoreCase) -ge 0 })
        if ($match.Count -gt 0) { return [string]$match[0].name }
    }
    return ""
}

function Write-Animset([string]$ModelPath, [string]$OutputPath, [string]$ModelId, [string]$SourceGame, [string]$AnimationSource, [bool]$Airborne) {
    $model = Get-Content -LiteralPath $ModelPath -Raw | ConvertFrom-Json
    $animations = @($model.animations)
    $clips = @()
    $categories = [ordered]@{ idle = @(); move = @(); attack = @(); hit = @(); status = @(); misc = @() }
    foreach ($animation in $animations) {
        $name = [string]$animation.name
        $category = Get-AnimationCategory $name
        $clip = [ordered]@{
            gltf_name = $name
            source = $AnimationSource
            action = "$name.gfbanm"
            base = $name
            phase = $(if ([bool]$animation.loop) { 'loop' } else { 'one_shot' })
            is_loop = [bool]$animation.loop
            category = $category
            frame_start = 0
            frame_end = [int]$animation.frame_count - 1
            duration_frames = [int]$animation.frame_count
            duration_seconds = [double]$animation.duration_seconds
            clip_key = "${AnimationSource}:name:$name"
        }
        $clips += [pscustomobject]$clip
        $categories[$category] += $name
    }
    $roles = [ordered]@{
        idle = Select-AnimationName $animations @('ba10_waitA01', 'kw01_wait01', 'fi01_wait01')
        move = Select-AnimationName $animations @('fi21_run01', 'fi20_walk01', 'kw33_move')
        attack1 = Select-AnimationName $animations @('ba20_buturi01', 'ba21_tokusyu01')
        faint = Select-AnimationName $animations @('ba41_down01')
        ground_idle = Select-AnimationName $animations @('ba10_waitA01', 'kw01_wait01')
    }
    if ($Airborne) {
        $roles.air_idle = Select-AnimationName $animations @('fi01_wait01', 'ba10_waitA01')
        $roles.land = Select-AnimationName $animations @('ba01_land01', 'ba01_landA01')
    }
    $document = [ordered]@{
        schema = 'animset-v3'
        model_id = $ModelId
        source_game = $SourceGame
        fps = 30
        clip_count = $clips.Count
        clips = $clips
        categories = $categories
        roles = $roles
        meta = $(if ($Airborne) { [ordered]@{ movementMode = 'airborne' } } else { [ordered]@{ movementMode = 'grounded' } })
        export_scope = [ordered]@{ native_model = 'gfbmdl'; native_animation = 'gfbanm'; lod = 0; material_variant = [string]$model.source.material_variant }
    }
    $document | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $OutputPath -Encoding utf8
}

function Validate-PhModel([string]$PathValue, [string]$ExpectedVariant) {
    $document = Get-Content -LiteralPath $PathValue -Raw | ConvertFrom-Json
    if ($document.schema -ne 'phlosion-native-model-ir-v1' -or $document.source.material_variant -ne $ExpectedVariant) {
        throw "Native model identity validation failed: $PathValue"
    }
    if ($document.coordinate_system.PSObject.Properties.Name -notcontains 'unit_scale_to_meters' -or
        [Math]::Abs(([double]$document.coordinate_system.unit_scale_to_meters) - 0.01) -gt 0.0000001) {
        throw "LGPE native model must declare its centimetre-to-metre conversion: $PathValue"
    }
    if ([int]$document.model.vertex_count -le 0 -or [int]$document.model.index_count -le 0 -or
        @($document.skeleton.bones).Count -le 0 -or @($document.animations).Count -le 0 -or
        @($document.materials).Count -le 0) {
        throw "Native model structural validation failed: $PathValue"
    }
    if ($ExpectedVariant -eq 'shiny' -and [string]::IsNullOrWhiteSpace([string]$document.source.material_source)) {
        throw "Shiny model lacks rare-material provenance: $PathValue"
    }
    return $document
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($GameRoot)) { $GameRoot = Join-Path $scriptRoot '..\..' }
$GameRoot = Resolve-FullPath $GameRoot
if ([string]::IsNullOrWhiteSpace($RecipePath)) { $RecipePath = Join-Path $scriptRoot 'gamefreak_pokemon_imports_lgpe.json' }
$RecipePath = Resolve-FullPath $RecipePath
if ([string]::IsNullOrWhiteSpace($DepotRoot)) { $DepotRoot = 'D:\ProjectData\Games\PokemonAutochess\Assets' }
$DepotRoot = Resolve-FullPath $DepotRoot
$projectDepot = Join-Path $DepotRoot 'pokemon-autochess'
$RomFsRoot = Resolve-FullPath $RomFsRoot

$recipe = Get-Content -LiteralPath $RecipePath -Raw | ConvertFrom-Json
if ($recipe.schema -ne 'phlosion-gamefreak-gfpak-import-recipe-v1') { throw "Unsupported recipe schema: $($recipe.schema)" }
$selected = @($recipe.imports)
if ($SpeciesId.Count -gt 0) { $selected = @($selected | Where-Object { $SpeciesId -contains [int]$_.speciesId }) }
if ($selected.Count -eq 0) { throw 'Recipe/filter selected no imports.' }

$jobs = @()
foreach ($item in $selected) {
    $archive = Join-Path $RomFsRoot (Join-Path ([string]$recipe.packageRootRelativePath) ($item.packageStem + '.gfpak'))
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) { throw "GFPAK source is missing: $archive" }
    foreach ($output in @($item.outputs)) {
        $jobs += [pscustomobject]@{ Item = $item; Output = $output; Archive = $archive }
    }
}
Write-Host "Game Freak GFPAK import plan for $($recipe.sourceGame): $($jobs.Count) canonical variants"
foreach ($job in $jobs) {
    Write-Host ("  #{0:D4} {1} {2}/{3} -> {4}" -f [int]$job.Item.speciesId, $job.Item.speciesName, $job.Item.genderLabel, $job.Output.appearance, $job.Output.stem)
}
if ($PlanOnly) { Write-Host 'Plan validated; no files were written.'; exit 0 }

$gftoolProject = Join-Path $GfToolRoot 'TrinityBatchExporter\TrinityBatchExporter.csproj'
$gftoolDll = Join-Path $GfToolRoot 'TrinityBatchExporter\bin\Release\net8.0-windows7.0\TrinityBatchExporter.dll'
$gfbmdlExporterProject = Join-Path $SwitchToolboxRoot 'PhlosionLgpeExporter\PhlosionLgpeExporter.csproj'
$gfbmdlExporter = Join-Path $SwitchToolboxRuntime 'PhlosionLgpeExporter.exe'
if (-not $SkipBuild) {
    $env:DOTNET_ROLL_FORWARD = 'Major'
    & dotnet build $gftoolProject -c Release -v:minimal
    if ($LASTEXITCODE -ne 0) { throw 'GFPAK extractor build failed.' }
    & dotnet build $gfbmdlExporterProject -c Release -v:minimal
    if ($LASTEXITCODE -ne 0) { throw 'Game Freak GFBMDL native exporter build failed.' }
}
foreach ($required in @($gftoolDll, $gfbmdlExporter)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required exporter is missing: $required" }
}

$hashEvidence = Join-Path $SwitchToolboxRoot 'File_Format_Library\Resources\Hashes\Pkmn.txt'
$sourceDepotFolder = if ($recipe.PSObject.Properties.Name -contains 'sourceDepotFolder') {
    [string]$recipe.sourceDepotFolder
} else {
    [string]$recipe.sourceGame
}
$animationSource = if ($recipe.PSObject.Properties.Name -contains 'animationSource') {
    [string]$recipe.animationSource
} else {
    'gamefreak-gfbanm'
}
if ([string]::IsNullOrWhiteSpace($sourceDepotFolder) -or $sourceDepotFolder.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
    throw "Invalid sourceDepotFolder: $sourceDepotFolder"
}
$sourceRoot = Join-Path $projectDepot ('source\gamefreak\' + $sourceDepotFolder + '\' + $recipe.sourceVersion + '\romfs')
$derivedRoot = Join-Path $projectDepot ('derived\imports\gamefreak\' + $recipe.sourceGame)
$depotModelsRoot = Join-Path $projectDepot 'runtime\assets\models'
$depotObjectsRoot = Join-Path $projectDepot 'runtime\content\phlosion\objects'
$gameModelsRoot = Join-Path $GameRoot 'assets\models'
$gameObjectsRoot = Join-Path $GameRoot 'content\phlosion\objects'
$forge = Join-Path $GameRoot 'build\Debug\PhlosionForge.exe'
if ($Cook -and -not (Test-Path -LiteralPath $forge -PathType Leaf)) { throw "PhlosionForge is missing: $forge" }

$tempParent = Resolve-FullPath ([IO.Path]::GetTempPath())
$tempRoot = Join-Path $tempParent ('phlosion-gfpak-import-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot | Out-Null
$reports = @()
try {
    foreach ($group in @($jobs | Group-Object { $_.Item.packageStem })) {
        $first = $group.Group[0]
        $item = $first.Item
        $packageStem = [string]$item.packageStem
        $packageRoot = Join-Path $tempRoot $packageStem
        $extractRoot = Join-Path $packageRoot 'extract'
        New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
        $sourceArchive = Join-Path $sourceRoot (Join-Path ([string]$recipe.packageRootRelativePath) ($packageStem + '.gfpak'))
        Publish-File $first.Archive $sourceArchive $sourceRoot
        $evidencePath = Join-Path $packageRoot 'name-evidence.txt'
        $evidence = @((Get-Content -LiteralPath $hashEvidence))
        $extraNameEvidence = if ($item.PSObject.Properties.Name -contains 'extraNameEvidence') {
            @($item.extraNameEvidence)
        } else {
            @()
        }
        foreach ($suffix in $extraNameEvidence) { $evidence += "${packageStem}_${suffix}.gfbanm" }
        Set-Content -LiteralPath $evidencePath -Value $evidence -Encoding utf8
        $extractManifest = Join-Path $packageRoot 'extract-manifest.json'
        $env:DOTNET_ROLL_FORWARD = 'Major'
        & dotnet $gftoolDll --gfpak $sourceArchive --output $extractRoot --manifest $extractManifest --name-evidence $evidencePath --pokemon-stem $packageStem --source-game ([string]$recipe.sourceGame)
        if ($LASTEXITCODE -ne 0) { throw "GFPAK extraction failed for $packageStem" }
        $resolvedRoot = Join-Path $extractRoot 'resolved'

        foreach ($job in $group.Group) {
            $appearance = [string]$job.Output.appearance
            $stem = [string]$job.Output.stem
            $modelName = if ($appearance -eq 'shiny') { $packageStem + '_rare.gfbmdl' } else { $packageStem + '.gfbmdl' }
            $modelPath = Join-Path $resolvedRoot $modelName
            if (-not (Test-Path -LiteralPath $modelPath -PathType Leaf)) { throw "Resolved model is missing: $modelPath" }
            $exportRoot = Join-Path $packageRoot $stem
            New-Item -ItemType Directory -Path $exportRoot -Force | Out-Null
            $outputModel = Join-Path $exportRoot ($stem + '.phmodel')
            $outputAnimset = Join-Path $exportRoot ($stem + '.animset.json')
            $sourceIdentity = $sourceArchive + '::resolved/' + $modelName
            $arguments = @('--model', $modelPath, '--resources', $extractRoot, '--output', $outputModel,
                '--source-game', [string]$recipe.sourceGame, '--material-variant', $appearance,
                '--source-model-identity', $sourceIdentity)
            if ($recipe.PSObject.Properties.Name -contains 'normalMapSpace') {
                $arguments += @('--normal-map-space', [string]$recipe.normalMapSpace)
            }
            if ($appearance -eq 'shiny') { $arguments += @('--material-source', $sourceIdentity) }
            & $gfbmdlExporter @arguments
            if ($LASTEXITCODE -ne 0) { throw "Native Game Freak GFBMDL export failed for $stem" }
            $airborne = ($item.PSObject.Properties.Name -contains 'airborne') -and [bool]$item.airborne
            Write-Animset $outputModel $outputAnimset $stem ([string]$recipe.sourceGame) $animationSource $airborne
            $document = Validate-PhModel $outputModel $appearance
            $payloadPath = Join-Path $exportRoot ($stem + '.bin')
            $texturePath = Join-Path $exportRoot ($stem + '_textures')
            foreach ($required in @($payloadPath, $outputAnimset, $texturePath)) {
                if (-not (Test-Path -LiteralPath $required)) { throw "Importer omitted required output: $required" }
            }
            Copy-Item -LiteralPath $extractManifest -Destination (Join-Path $exportRoot 'source-extract-manifest.json') -Force
            $canonicalPath = Join-Path $derivedRoot ($stem + '_native-ir')
            if ((Test-Path -LiteralPath $canonicalPath) -and -not $Force) { throw "Canonical import exists; pass -Force: $canonicalPath" }
            Publish-Directory $exportRoot $canonicalPath $derivedRoot
            if (-not $SkipPublish) {
                foreach ($destinationRoot in @($depotModelsRoot, $gameModelsRoot)) {
                    Publish-NativeModelSharedPayload `
                        -SourceManifestPath $outputModel `
                        -SourceModelsRoot $exportRoot `
                        -DestinationModelsRoot $destinationRoot `
                        -DestinationManifestName ($stem + '.phmodel') | Out-Null
                    Publish-File $outputAnimset (Join-Path $destinationRoot ($stem + '.animset.json')) $destinationRoot
                    Publish-Directory $texturePath (Join-Path $destinationRoot ($stem + '_textures')) $destinationRoot
                }
            }
            if ($Cook) {
                $cookOutput = @(& $forge cook-model ("assets/models/" + $stem + '.phmodel') 2>&1)
                $cookExitCode = $LASTEXITCODE
                foreach ($line in $cookOutput) { Write-Host ([string]$line) }
                if ($cookExitCode -ne 0) { throw "PhlosionForge failed for $stem" }

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
                    throw "Could not identify exactly one cooked object for $stem from PhlosionForge output."
                }
                $cookedObjectName = [string]$cookedObjectNames[0]
                $cookedObjectPath = Assert-PathUnderRoot `
                    (Join-Path $gameObjectsRoot $cookedObjectName) `
                    $gameObjectsRoot `
                    'Cooked object'
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
            $reports += [pscustomobject]@{
                species_id = [int]$item.speciesId; species_name = [string]$item.speciesName
                gender = [string]$item.genderLabel; appearance = $appearance; stem = $stem
                source_archive = $sourceArchive; source_model = $sourceIdentity
                vertex_count = [int]$document.model.vertex_count; index_count = [int]$document.model.index_count
                submesh_count = [int]$document.model.submesh_count; material_count = @($document.materials).Count
                bone_count = @($document.skeleton.bones).Count; animation_count = @($document.animations).Count
                canonical_path = $canonicalPath
            }
        }
    }
    if (-not $SkipPublish) {
        foreach ($destinationRoot in @($depotModelsRoot, $gameModelsRoot)) {
            $payloadGarbage = Remove-UnreferencedNativeModelPayloads -ModelsRoot $destinationRoot
            if ($payloadGarbage.removed_file_count -gt 0) {
                Write-Host ("Removed {0} unreferenced shared native payloads ({1} bytes) from {2}." -f `
                    $payloadGarbage.removed_file_count, `
                    $payloadGarbage.removed_bytes, `
                    $destinationRoot)
            }
        }
    }
    $report = [ordered]@{
        schema = 'phlosion-gamefreak-import-report-v1'; generated_utc = [DateTimeOffset]::UtcNow.ToString('o')
        source_game = [string]$recipe.sourceGame; recipe = $RecipePath; import_count = $reports.Count; imports = $reports
    }
    New-Item -ItemType Directory -Path $derivedRoot -Force | Out-Null
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $derivedRoot 'latest-import-report.json') -Encoding utf8
    Write-Host "Imported, validated, and published $($reports.Count) Game Freak GFPAK variants from $($recipe.sourceGame)."
} finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Assert-PathUnderRoot $tempRoot $tempParent 'Importer temporary directory' | Out-Null
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
