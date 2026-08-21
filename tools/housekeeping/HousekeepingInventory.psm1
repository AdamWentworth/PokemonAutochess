Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$PathValue)

    return [IO.Path]::GetFullPath($PathValue)
}

function ConvertTo-PortablePath {
    param([Parameter(Mandatory = $true)][string]$PathValue)

    return $PathValue.Replace([IO.Path]::DirectorySeparatorChar, '/')
}

function Get-RelativeInventoryPath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$PathValue
    )

    $rootPath = (Resolve-FullPath $Root).TrimEnd('\', '/')
    $fullPath = Resolve-FullPath $PathValue
    $prefix = $rootPath + [IO.Path]::DirectorySeparatorChar
    if ($fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        return ConvertTo-PortablePath $fullPath.Substring($prefix.Length)
    }
    return ConvertTo-PortablePath $fullPath
}

function Get-OptionalProperty {
    param(
        [Parameter(Mandatory = $true)][object]$Object,
        [Parameter(Mandatory = $true)][string]$Name,
        $Default = $null
    )

    if ($null -ne $Object -and $Object.PSObject.Properties.Name -contains $Name) {
        return $Object.$Name
    }
    return $Default
}

function New-StringSet {
    return New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
}

function Get-GitSnapshot {
    param([Parameter(Mandatory = $true)][string]$Root)

    if (-not (Test-Path -LiteralPath (Join-Path $Root '.git'))) {
        return [pscustomobject][ordered]@{
            available = $false
            commit = $null
            branch = $null
            dirty = $null
            status = @()
        }
    }

    $commit = (& git -C $Root rev-parse HEAD 2>$null | Select-Object -First 1)
    $branch = (& git -C $Root branch --show-current 2>$null | Select-Object -First 1)
    $status = @(& git -C $Root status --short 2>$null)
    return [pscustomobject][ordered]@{
        available = $true
        commit = [string]$commit
        branch = [string]$branch
        dirty = $status.Count -gt 0
        status = @($status | ForEach-Object { [string]$_ })
    }
}

function Get-DirectoryByteCount {
    param([Parameter(Mandatory = $true)][string]$PathValue)

    if (-not (Test-Path -LiteralPath $PathValue -PathType Container)) {
        return [int64]0
    }
    $measurement = Get-ChildItem -LiteralPath $PathValue -Recurse -Force -File -ErrorAction SilentlyContinue |
        Measure-Object -Property Length -Sum
    if ($null -eq $measurement.Sum) {
        return [int64]0
    }
    return [int64]$measurement.Sum
}

function Get-WorkspaceDirectoryInventory {
    param([Parameter(Mandatory = $true)][string]$Root)

    $names = @(
        'build', 'build-vs2022', 'build-ninja', 'build-fetch-deps',
        'build-fetch', 'build-assetless', 'artifacts', 'cache', '.phlosion',
        'debug', 'assets', 'content'
    )
    $records = foreach ($name in $names) {
        $path = Join-Path $Root $name
        if (-not (Test-Path -LiteralPath $path -PathType Container)) {
            continue
        }
        [pscustomobject][ordered]@{
            path = $name
            bytes = Get-DirectoryByteCount $path
            category = switch -Regex ($name) {
                '^assets$' { 'authoritative_or_private'; break }
                '^content$' { 'generated_project_content'; break }
                '^artifacts$' { 'review_before_cleanup'; break }
                '^build$' { 'active_build'; break }
                default { 'regenerable_candidate' }
            }
        }
    }
    return @($records)
}

function Get-KeyBuildArtifacts {
    param(
        [Parameter(Mandatory = $true)][string]$GameRoot,
        [string]$EngineRoot,
        [switch]$IncludeHash
    )

    $candidates = @(
        [pscustomobject]@{ owner = 'game'; path = 'build/Debug/PokemonAutochess.exe'; configuration = 'Debug'; role = 'game' },
        [pscustomobject]@{ owner = 'game'; path = 'build/Release/PokemonAutochess.exe'; configuration = 'Release'; role = 'game' },
        [pscustomobject]@{ owner = 'game'; path = 'build/Debug/PAC_Tests.exe'; configuration = 'Debug'; role = 'tests' },
        [pscustomobject]@{ owner = 'game'; path = 'build/Release/PAC_Tests.exe'; configuration = 'Release'; role = 'tests' },
        [pscustomobject]@{ owner = 'game'; path = 'build/Debug/PhlosionForge.exe'; configuration = 'Debug'; role = 'cook_tool' },
        [pscustomobject]@{ owner = 'game'; path = '.phlosion/editor/Debug/PokemonAutochessEditorProject.dll'; configuration = 'Debug'; role = 'editor_plugin' },
        [pscustomobject]@{ owner = 'game'; path = '.phlosion/editor/Release/PokemonAutochessEditorProject.dll'; configuration = 'Release'; role = 'editor_plugin' }
    )
    if (-not [string]::IsNullOrWhiteSpace($EngineRoot) -and (Test-Path -LiteralPath $EngineRoot)) {
        $candidates += @(
            [pscustomobject]@{ owner = 'engine'; path = 'build/Debug/PhlosionEditor.exe'; configuration = 'Debug'; role = 'editor' },
            [pscustomobject]@{ owner = 'engine'; path = 'build/Release/PhlosionEditor.exe'; configuration = 'Release'; role = 'editor' }
        )
    }

    $records = foreach ($candidate in $candidates) {
        $root = if ($candidate.owner -eq 'engine') { $EngineRoot } else { $GameRoot }
        $nativePath = $candidate.path.Replace('/', [IO.Path]::DirectorySeparatorChar)
        $fullPath = Join-Path $root $nativePath
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            [pscustomobject][ordered]@{
                owner = $candidate.owner
                path = $candidate.path
                configuration = $candidate.configuration
                role = $candidate.role
                exists = $false
                bytes = $null
                last_write_utc = $null
                sha256 = $null
            }
            continue
        }
        $file = Get-Item -LiteralPath $fullPath
        $hash = $null
        if ($IncludeHash) {
            $hash = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        [pscustomobject][ordered]@{
            owner = $candidate.owner
            path = $candidate.path
            configuration = $candidate.configuration
            role = $candidate.role
            exists = $true
            bytes = [int64]$file.Length
            last_write_utc = $file.LastWriteTimeUtc.ToString('o')
            sha256 = $hash
        }
    }
    return @($records)
}

function Get-GameEditorOwnershipInventory {
    param([Parameter(Mandatory = $true)][string]$GameRoot)

    $relativePaths = @(
        'build/Debug/PhlosionEditor.exe',
        'build/Debug/PhlosionEditor.pdb',
        'build/Release/PhlosionEditor.exe',
        'build/Release/PhlosionEditor.pdb',
        'build/PhlosionEditor.dir',
        'build/phlosion-engine/PhlosionEditor.dir',
        'build/phlosion-engine/PhlosionEditor.vcxproj',
        'build/phlosion-engine/PhlosionEditor.vcxproj.filters',
        'build/phlosion-engine/PhlosionEditor.vcxproj.user'
    )
    $artifacts = @($relativePaths | ForEach-Object {
        $relativePath = $_
        $fullPath = Join-Path $GameRoot $relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar)
        if (Test-Path -LiteralPath $fullPath) {
            $item = Get-Item -LiteralPath $fullPath -Force
            [pscustomobject][ordered]@{
                path = $relativePath
                kind = if ($item.PSIsContainer) { 'directory' } else { 'file' }
                bytes = if ($item.PSIsContainer) {
                    Get-DirectoryByteCount $fullPath
                } else {
                    [int64]$item.Length
                }
                last_write_utc = $item.LastWriteTimeUtc.ToString('o')
            }
        }
    })

    $cachePath = Join-Path $GameRoot 'build/CMakeCache.txt'
    $buildEditorEnabled = $null
    if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
        $setting = Get-Content -LiteralPath $cachePath |
            Where-Object { $_ -match '^PHLOSION_BUILD_EDITOR:BOOL=' } |
            Select-Object -First 1
        if ($setting -match '=([^=]+)$') {
            $buildEditorEnabled = $Matches[1] -match '^(ON|TRUE|YES|1)$'
        }
    }

    return [pscustomobject][ordered]@{
        expected_owner = 'engine'
        game_build_editor_enabled = $buildEditorEnabled
        stale_artifact_count = $artifacts.Count
        stale_artifact_bytes = if ($artifacts.Count -gt 0) {
            [int64](($artifacts.bytes | Measure-Object -Sum).Sum)
        } else {
            [int64]0
        }
        stale_artifacts = $artifacts
    }
}

function Get-CtestCatalog {
    param([Parameter(Mandatory = $true)][string]$GameRoot)

    $buildRoot = Join-Path $GameRoot 'build'
    $ctest = Get-Command ctest -ErrorAction SilentlyContinue
    if ($null -eq $ctest -or -not (Test-Path -LiteralPath $buildRoot -PathType Container)) {
        return [pscustomobject][ordered]@{
            available = $false
            configuration = 'Debug'
            test_count = 0
            tests = @()
            note = 'ctest or the active build directory is unavailable'
        }
    }

    $output = @(& $ctest.Source --test-dir $buildRoot -C Debug -N 2>&1 | ForEach-Object { [string]$_ })
    $tests = @($output | ForEach-Object {
        if ($_ -match '^\s*Test\s+#\d+:\s+(.+?)\s*$') {
            $Matches[1]
        }
    })
    return [pscustomobject][ordered]@{
        available = $LASTEXITCODE -eq 0
        configuration = 'Debug'
        test_count = $tests.Count
        tests = $tests
        note = 'Catalog only; this inventory does not execute the tests or claim they pass.'
    }
}

function Get-PokemonConfigInventory {
    param([Parameter(Mandatory = $true)][string]$GameRoot)

    $relativePath = 'config/pokemon_config.json'
    $fullPath = Join-Path $GameRoot ($relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))
    $document = Get-Content -LiteralPath $fullPath -Raw | ConvertFrom-Json
    $modelRecords = @()

    foreach ($speciesProperty in @($document.PSObject.Properties)) {
        $species = [string]$speciesProperty.Name
        $entry = $speciesProperty.Value
        $baseModel = [string](Get-OptionalProperty $entry 'model' '')
        if (-not [string]::IsNullOrWhiteSpace($baseModel)) {
            $modelRecords += [pscustomobject][ordered]@{
                species = $species
                variant = 'default'
                model = ConvertTo-PortablePath $baseModel
                stem = [IO.Path]::GetFileNameWithoutExtension($baseModel)
                extension = [IO.Path]::GetExtension($baseModel).ToLowerInvariant()
            }
        }
        $variants = Get-OptionalProperty $entry 'modelVariants'
        if ($null -ne $variants) {
            foreach ($variantProperty in @($variants.PSObject.Properties)) {
                $model = [string]$variantProperty.Value
                if ([string]::IsNullOrWhiteSpace($model)) {
                    continue
                }
                $modelRecords += [pscustomobject][ordered]@{
                    species = $species
                    variant = [string]$variantProperty.Name
                    model = ConvertTo-PortablePath $model
                    stem = [IO.Path]::GetFileNameWithoutExtension($model)
                    extension = [IO.Path]::GetExtension($model).ToLowerInvariant()
                }
            }
        }
    }

    $uniqueModels = @($modelRecords | Group-Object model | ForEach-Object {
        $sample = $_.Group[0]
        $assetPath = if ($sample.model.Contains('/')) {
            Join-Path $GameRoot ($sample.model.Replace('/', [IO.Path]::DirectorySeparatorChar))
        } else {
            Join-Path (Join-Path $GameRoot 'assets/models') $sample.model
        }
        [pscustomobject][ordered]@{
            model = $sample.model
            stem = $sample.stem
            extension = $sample.extension
            species = @($_.Group.species | Sort-Object -Unique)
            variants = @($_.Group.variant | Sort-Object -Unique)
            exists = Test-Path -LiteralPath $assetPath -PathType Leaf
        }
    } | Sort-Object model)

    return [pscustomobject][ordered]@{
        path = $relativePath
        species_count = @($document.PSObject.Properties).Count
        model_bindings = @($modelRecords)
        unique_models = $uniqueModels
        unique_model_count = $uniqueModels.Count
        missing_models = @($uniqueModels | Where-Object { -not $_.exists })
        extension_counts = @($uniqueModels | Group-Object extension | ForEach-Object {
            [pscustomobject][ordered]@{ extension = $_.Name; count = $_.Count }
        })
    }
}

function Get-RecipeInventory {
    param([Parameter(Mandatory = $true)][string]$GameRoot)

    $packageRoot = Join-Path $GameRoot 'config/assets'
    $recipeFiles = @(Get-ChildItem -LiteralPath $packageRoot -File -Filter '*_native_model_package.json' |
        Sort-Object Name)
    $recipeRecords = @()
    $outputRecords = @()

    foreach ($file in $recipeFiles) {
        $document = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
        $outputsForRecipe = @()
        foreach ($import in @(Get-OptionalProperty $document 'imports' @())) {
            foreach ($output in @(Get-OptionalProperty $import 'outputs' @())) {
                $stem = [string](Get-OptionalProperty $output 'stem' '')
                if ([string]::IsNullOrWhiteSpace($stem)) {
                    continue
                }
                $modelPath = Join-Path (Join-Path $GameRoot 'assets/models') ($stem + '.phmodel')
                $record = [pscustomobject][ordered]@{
                    recipe = Get-RelativeInventoryPath $GameRoot $file.FullName
                    recipe_schema = [string](Get-OptionalProperty $document 'schema' '')
                    source_game = [string](Get-OptionalProperty $document 'sourceGame' '')
                    source_version = [string](Get-OptionalProperty $document 'sourceVersion' '')
                    species_id = [int](Get-OptionalProperty $import 'speciesId' 0)
                    species_name = [string](Get-OptionalProperty $import 'speciesName' '')
                    gender = [string](Get-OptionalProperty $import 'genderLabel' '')
                    appearance = [string](Get-OptionalProperty $output 'appearance' '')
                    stem = $stem
                    published = Test-Path -LiteralPath $modelPath -PathType Leaf
                }
                $outputsForRecipe += $record
                $outputRecords += $record
            }
        }
        $recipeRecords += [pscustomobject][ordered]@{
            path = Get-RelativeInventoryPath $GameRoot $file.FullName
            schema = [string](Get-OptionalProperty $document 'schema' '')
            source_game = [string](Get-OptionalProperty $document 'sourceGame' '')
            source_version = [string](Get-OptionalProperty $document 'sourceVersion' '')
            import_count = @(Get-OptionalProperty $document 'imports' @()).Count
            output_count = $outputsForRecipe.Count
            published_output_count = @($outputsForRecipe | Where-Object published).Count
        }
    }

    $duplicates = @($outputRecords | Group-Object stem | Where-Object Count -gt 1 | ForEach-Object {
        [pscustomobject][ordered]@{
            stem = $_.Name
            declarations = @($_.Group)
        }
    })

    return [pscustomobject][ordered]@{
        recipes = @($recipeRecords)
        outputs = @($outputRecords | Sort-Object stem, recipe)
        output_count = $outputRecords.Count
        published_output_count = @($outputRecords | Where-Object published).Count
        unpublished_output_count = @($outputRecords | Where-Object { -not $_.published }).Count
        duplicate_stems = $duplicates
    }
}

function Get-AssetCatalogInventory {
    param(
        [Parameter(Mandatory = $true)][string]$GameRoot,
        [Parameter(Mandatory = $true)][object]$Recipes
    )

    $relativePath = 'config/assets/asset_catalog.json'
    $fullPath = Join-Path $GameRoot $relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar)
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Asset catalog not found: $relativePath"
    }
    $document = Get-Content -LiteralPath $fullPath -Raw | ConvertFrom-Json
    if ([string](Get-OptionalProperty $document 'kind' '') -ne 'pokemon_autochess_asset_catalog' -or
        [int](Get-OptionalProperty $document 'schema_version' 0) -ne 1) {
        throw "Unsupported asset catalog schema: $relativePath"
    }

    $nativeModels = @()
    foreach ($importSet in @(Get-OptionalProperty $document 'native_import_sets' @())) {
        $recipePath = ConvertTo-PortablePath ([string](Get-OptionalProperty $importSet 'recipe' ''))
        $selection = [string](Get-OptionalProperty $importSet 'selection' '')
        $recipeOutputs = @($Recipes.outputs | Where-Object recipe -eq $recipePath)
        if ($recipeOutputs.Count -eq 0) {
            throw "Asset catalog import set has no matching recipe outputs: $recipePath"
        }
        $selected = if ($selection -eq 'all_outputs') {
            $recipeOutputs
        } elseif ($selection -eq 'include_stems') {
            $stems = @($importSet.stems | ForEach-Object { [string]$_ })
            $missing = @($stems | Where-Object { $recipeOutputs.stem -notcontains $_ })
            if ($missing.Count -gt 0) {
                throw "Asset catalog selects undeclared recipe stems: $($missing -join ', ')"
            }
            @($recipeOutputs | Where-Object { $stems -contains $_.stem })
        } else {
            throw "Unsupported asset catalog recipe selection '$selection': $recipePath"
        }
        foreach ($output in @($selected)) {
            $nativeModels += [pscustomobject][ordered]@{
                stem = [string]$output.stem
                source = 'assets/models/' + [string]$output.stem + '.phmodel'
                animset = 'assets/models/' + [string]$output.stem + '.animset.json'
                owner = $recipePath
                source_game = [string]$output.source_game
                purpose = 'native_import'
            }
        }
    }
    foreach ($model in @(Get-OptionalProperty $document 'explicit_native_models' @())) {
        $nativeModels += [pscustomobject][ordered]@{
            stem = [string]$model.stem
            source = ConvertTo-PortablePath ([string]$model.source)
            animset = ConvertTo-PortablePath ([string]$model.animset)
            owner = $relativePath
            source_game = ''
            purpose = [string](Get-OptionalProperty $model 'purpose' 'explicit_native_model')
        }
    }
    $duplicateNative = @($nativeModels | Group-Object stem | Where-Object Count -gt 1)
    if ($duplicateNative.Count -gt 0) {
        throw "Asset catalog contains duplicate native stems: $($duplicateNative.Name -join ', ')"
    }

    $authored = @(Get-OptionalProperty $document 'authored_runtime_sources' @() | ForEach-Object {
        [pscustomobject][ordered]@{
            id = [string]$_.id
            source = ConvertTo-PortablePath ([string]$_.source)
            prefab_kind = [string]$_.prefab_kind
            purpose = [string]$_.purpose
            migration = [string]$_.migration
        }
    })
    $retained = @(Get-OptionalProperty $document 'retained_review_sources' @() | ForEach-Object {
        [pscustomobject][ordered]@{
            id = [string]$_.id
            source = ConvertTo-PortablePath ([string]$_.source)
            animset = ConvertTo-PortablePath ([string]$_.animset)
            disposition = [string]$_.disposition
            replacement_stems = @((Get-OptionalProperty $_ 'replacement_stems' @()) | ForEach-Object { [string]$_ })
            legacy_cooked_identities = @((Get-OptionalProperty $_ 'legacy_cooked_identities' @()) | ForEach-Object { [string]$_ })
        }
    })
    $environments = @(Get-OptionalProperty $document 'environment_resources' @() | ForEach-Object {
        [pscustomobject][ordered]@{
            id = [string]$_.id
            scene = ConvertTo-PortablePath ([string]$_.scene)
            authored_scene = ConvertTo-PortablePath ([string]$_.authored_scene)
            cooked_object_root = ConvertTo-PortablePath ([string]$_.cooked_object_root)
        }
    })

    return [pscustomobject][ordered]@{
        path = $relativePath
        schema_version = 1
        pokemon_config = ConvertTo-PortablePath ([string]$document.pokemon_config)
        native_models = @($nativeModels | Sort-Object stem)
        native_model_count = $nativeModels.Count
        authored_runtime_sources = $authored
        authored_runtime_source_count = $authored.Count
        retained_review_sources = $retained
        retained_review_source_count = $retained.Count
        environment_resources = $environments
        environment_resource_count = $environments.Count
    }
}

function Get-TrackedGlbReferences {
    param([Parameter(Mandatory = $true)][string]$GameRoot)

    $extensions = New-StringSet
    @('.c', '.cc', '.cpp', '.h', '.hpp', '.inl', '.json', '.md', '.ps1', '.cmake',
      '.txt', '.glsl', '.vert', '.frag') | ForEach-Object { [void]$extensions.Add($_) }
    $tracked = @(& git -C $GameRoot ls-files 2>$null)
    $records = @()
    foreach ($relative in $tracked) {
        $extension = [IO.Path]::GetExtension([string]$relative)
        if (-not $extensions.Contains($extension)) {
            continue
        }
        $fullPath = Join-Path $GameRoot ([string]$relative).Replace('/', [IO.Path]::DirectorySeparatorChar)
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            continue
        }
        foreach ($match in @(Select-String -LiteralPath $fullPath -SimpleMatch '.glb' -ErrorAction SilentlyContinue)) {
            $portable = ConvertTo-PortablePath ([string]$relative)
            $area = if ($portable.StartsWith('src/')) { 'runtime' }
                elseif ($portable.StartsWith('config/')) { 'runtime_config' }
                elseif ($portable.StartsWith('tests/')) { 'test' }
                elseif ($portable.StartsWith('tools/')) { 'tool' }
                elseif ($portable.StartsWith('docs/')) { 'documentation' }
                else { 'other' }
            $text = ([string]$match.Line).Trim()
            if ($text.Length -gt 300) {
                $text = $text.Substring(0, 300) + '...'
            }
            $records += [pscustomobject][ordered]@{
                path = $portable
                line = [int]$match.LineNumber
                area = $area
                text = $text
            }
        }
    }
    return @($records | Sort-Object path, line)
}

function Get-SourceAssetInventory {
    param(
        [Parameter(Mandatory = $true)][string]$GameRoot,
        [Parameter(Mandatory = $true)][object]$PokemonConfig,
        [Parameter(Mandatory = $true)][object[]]$GlbReferences,
        [Parameter(Mandatory = $true)][object]$AssetCatalog
    )

    $modelsRoot = Join-Path $GameRoot 'assets/models'
    $meshesRoot = Join-Path $GameRoot 'assets/meshes'
    $activeStems = New-StringSet
    foreach ($model in @($PokemonConfig.unique_models)) {
        [void]$activeStems.Add([string]$model.stem)
    }
    $catalogStems = New-StringSet
    foreach ($model in @($AssetCatalog.native_models)) {
        [void]$catalogStems.Add([string]$model.stem)
    }
    $authoredByPath = @{}
    foreach ($source in @($AssetCatalog.authored_runtime_sources)) {
        $authoredByPath[[string]$source.source] = $source
    }
    $retainedByPath = @{}
    foreach ($source in @($AssetCatalog.retained_review_sources)) {
        $retainedByPath[[string]$source.source] = $source
    }

    $modelFiles = if (Test-Path -LiteralPath $modelsRoot) {
        @(Get-ChildItem -LiteralPath $modelsRoot -File | Sort-Object Name)
    } else { @() }
    $nativeModels = @($modelFiles | Where-Object Extension -eq '.phmodel' | ForEach-Object {
        $stem = $_.BaseName
        $classification = if ($activeStems.Contains($stem)) { 'active_gameplay' }
            elseif ($catalogStems.Contains($stem)) { 'staged_import' }
            else { 'unclassified_native' }
        [pscustomobject][ordered]@{
            path = Get-RelativeInventoryPath $GameRoot $_.FullName
            stem = $stem
            bytes = [int64]$_.Length
            classification = $classification
        }
    })

    $animsets = @($modelFiles | Where-Object { $_.Name.EndsWith('.animset.json', [StringComparison]::OrdinalIgnoreCase) } | ForEach-Object {
        $stem = $_.Name.Substring(0, $_.Name.Length - '.animset.json'.Length)
        $classification = if ($activeStems.Contains($stem)) { 'active_gameplay' }
            elseif ($catalogStems.Contains($stem)) { 'staged_import' }
            elseif ($retainedByPath.ContainsKey('assets/models/' + $stem + '.glb')) { 'retained_glb_companion' }
            else { 'unclassified_animset' }
        [pscustomobject][ordered]@{
            path = Get-RelativeInventoryPath $GameRoot $_.FullName
            stem = $stem
            bytes = [int64]$_.Length
            classification = $classification
        }
    })

    $glbFiles = @($modelFiles | Where-Object Extension -eq '.glb')
    if (Test-Path -LiteralPath $meshesRoot) {
        $glbFiles += @(Get-ChildItem -LiteralPath $meshesRoot -File -Filter '*.glb' | Sort-Object Name)
    }
    $glbs = @($glbFiles | ForEach-Object {
        $file = $_
        $relative = Get-RelativeInventoryPath $GameRoot $file.FullName
        $matchingReferences = @($GlbReferences | Where-Object { $_.text.IndexOf($file.Name, [StringComparison]::OrdinalIgnoreCase) -ge 0 })
        $runtimeReferences = @($matchingReferences | Where-Object { $_.area -in @('runtime', 'runtime_config') })
        $testReferences = @($matchingReferences | Where-Object area -eq 'test')
        $catalogOwner = $null
        $classification = if ($authoredByPath.ContainsKey($relative)) {
            $catalogOwner = $authoredByPath[$relative]
            if ([string]$catalogOwner.purpose -eq 'capture_model') {
                'active_capture_source'
            } else {
                'active_authored_vfx_source'
            }
        } elseif ($retainedByPath.ContainsKey($relative)) {
            $catalogOwner = $retainedByPath[$relative]
            if ([string]$catalogOwner.disposition -like 'remove_*') {
                'legacy_model_candidate'
            } else {
                'retained_model_source'
            }
        } elseif ($runtimeReferences.Count -gt 0) {
            'uncatalogued_runtime_interchange'
        } else {
            'unclassified_glb'
        }
        [pscustomobject][ordered]@{
            path = $relative
            stem = $file.BaseName
            bytes = [int64]$file.Length
            classification = $classification
            catalog_owner = if ($null -ne $catalogOwner) { [string]$catalogOwner.id } else { $null }
            companion_animset = if (Test-Path -LiteralPath (Join-Path $modelsRoot ($file.BaseName + '.animset.json')) -PathType Leaf) {
                'assets/models/' + $file.BaseName + '.animset.json'
            } else { $null }
            runtime_reference_count = $runtimeReferences.Count
            test_reference_count = $testReferences.Count
            references = @($matchingReferences)
        }
    } | Sort-Object path)

    $extensionCounts = @($modelFiles | Group-Object { $_.Extension.ToLowerInvariant() } | ForEach-Object {
        [pscustomobject][ordered]@{
            extension = $_.Name
            count = $_.Count
            bytes = [int64](($_.Group.Length | Measure-Object -Sum).Sum)
        }
    } | Sort-Object count -Descending)

    return [pscustomobject][ordered]@{
        model_files = @($nativeModels)
        model_file_count = $nativeModels.Count
        animsets = $animsets
        animset_count = $animsets.Count
        glbs = $glbs
        glb_count = $glbs.Count
        extension_counts = $extensionCounts
        unclassified_native_models = @($nativeModels | Where-Object classification -eq 'unclassified_native')
        unclassified_animsets = @($animsets | Where-Object classification -eq 'unclassified_animset')
        unclassified_glbs = @($glbs | Where-Object { $_.classification -like 'unclassified*' -or $_.classification -like 'uncatalogued*' })
        legacy_glb_animsets = @($animsets | Where-Object classification -eq 'retained_glb_companion')
        legacy_model_candidates = @($glbs | Where-Object { $_.classification -like 'legacy_model_candidate*' })
    }
}

function Get-CookManifestInventory {
    param(
        [Parameter(Mandatory = $true)][string]$GameRoot,
        [Parameter(Mandatory = $true)][object]$PokemonConfig,
        [Parameter(Mandatory = $true)][object]$AssetCatalog
    )

    $relativePath = 'content/phlosion/cook_manifest.json'
    $fullPath = Join-Path $GameRoot ($relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        return [pscustomobject][ordered]@{
            path = $relativePath
            exists = $false
            kind = $null
            schema_version = 0
            pokemon_count = 0
            staged_count = 0
            auxiliary_count = 0
            retained_review_count = 0
            shared_dependency_count = 0
            catalog_matches = $false
            missing_sources = @()
            missing_objects = @()
            active_models_not_listed = @($PokemonConfig.unique_models | ForEach-Object { $_.model })
            shared_dependencies = @()
            entries = @()
        }
    }

    $document = Get-Content -LiteralPath $fullPath -Raw | ConvertFrom-Json
    $pokemonEntries = @(Get-OptionalProperty $document 'pokemon' @())
    $stagedEntries = @(Get-OptionalProperty $document 'staged_imports' @())
    $auxiliaryEntries = @(Get-OptionalProperty $document 'runtime_auxiliary_objects' @())
    $retainedEntries = @(Get-OptionalProperty $document 'retained_review_sources' @())
    $sharedDependencyEntries = @(Get-OptionalProperty $document 'shared_dependencies' @())
    $entries = @()
    foreach ($group in @(
        [pscustomobject]@{ scope = 'active_gameplay'; values = $pokemonEntries },
        [pscustomobject]@{ scope = 'staged_import'; values = $stagedEntries },
        [pscustomobject]@{ scope = 'authored_runtime'; values = $auxiliaryEntries })) {
        foreach ($entry in @($group.values)) {
            $source = ConvertTo-PortablePath ([string](Get-OptionalProperty $entry 'source' ''))
            $object = ConvertTo-PortablePath ([string](Get-OptionalProperty $entry 'object' ''))
            $sourceFullPath = Join-Path $GameRoot $source.Replace('/', [IO.Path]::DirectorySeparatorChar)
            $objectFullPath = Join-Path $GameRoot $object.Replace('/', [IO.Path]::DirectorySeparatorChar)
            $entries += [pscustomobject][ordered]@{
                scope = $group.scope
                source = $source
                object = $object
                source_exists = -not [string]::IsNullOrWhiteSpace($source) -and (Test-Path -LiteralPath $sourceFullPath -PathType Leaf)
                object_exists = -not [string]::IsNullOrWhiteSpace($object) -and (Test-Path -LiteralPath $objectFullPath -PathType Leaf)
            }
        }
    }
    $listedSourceNames = New-StringSet
    foreach ($entry in @($entries | Where-Object scope -eq 'active_gameplay')) {
        [void]$listedSourceNames.Add([IO.Path]::GetFileName([string]$entry.source))
    }
    $activeNotListed = @($PokemonConfig.unique_models | Where-Object { -not $listedSourceNames.Contains([string]$_.model) })
    $catalogRecord = Get-OptionalProperty $document 'asset_catalog'
    $catalogMatches = $null -ne $catalogRecord -and
        [string](Get-OptionalProperty $catalogRecord 'source' '') -eq [string]$AssetCatalog.path -and
        [int](Get-OptionalProperty $catalogRecord 'native_model_count' -1) -eq [int]$AssetCatalog.native_model_count -and
        [int](Get-OptionalProperty $catalogRecord 'authored_runtime_source_count' -1) -eq [int]$AssetCatalog.authored_runtime_source_count
    $sharedDependencies = @($sharedDependencyEntries | ForEach-Object {
        $assetId = ConvertTo-PortablePath ([string](Get-OptionalProperty $_ 'asset_id' ''))
        $path = ConvertTo-PortablePath ([string](Get-OptionalProperty $_ 'path' ''))
        [pscustomobject][ordered]@{
            asset_id = $assetId
            path = $path
            fnv1a64 = [string](Get-OptionalProperty $_ 'fnv1a64' '')
            bytes = [int64](Get-OptionalProperty $_ 'bytes' 0)
            exists = -not [string]::IsNullOrWhiteSpace($path) -and
                (Test-Path -LiteralPath (Join-Path $GameRoot $path.Replace('/', [IO.Path]::DirectorySeparatorChar)) -PathType Leaf)
        }
    })

    return [pscustomobject][ordered]@{
        path = $relativePath
        exists = $true
        kind = [string](Get-OptionalProperty $document 'kind' '')
        schema_version = [int](Get-OptionalProperty $document 'schema_version' 0)
        pokemon_count = $pokemonEntries.Count
        staged_count = $stagedEntries.Count
        auxiliary_count = $auxiliaryEntries.Count
        retained_review_count = $retainedEntries.Count
        shared_dependency_count = $sharedDependencies.Count
        catalog_matches = $catalogMatches
        missing_sources = @($entries | Where-Object { -not $_.source_exists })
        missing_objects = @($entries | Where-Object { -not $_.object_exists })
        active_models_not_listed = $activeNotListed
        shared_dependencies = $sharedDependencies
        entries = $entries
    }
}

function Get-CookedObjectInventory {
    param(
        [Parameter(Mandatory = $true)][string]$GameRoot,
        [Parameter(Mandatory = $true)][object]$PokemonConfig,
        [Parameter(Mandatory = $true)][object]$AssetCatalog,
        [Parameter(Mandatory = $true)][object]$CookManifest
    )

    $objectsRoot = Join-Path $GameRoot 'content/phlosion/objects'
    if (-not (Test-Path -LiteralPath $objectsRoot -PathType Container)) {
        return [pscustomobject][ordered]@{
            root = 'content/phlosion/objects'
            object_count = 0
            objects = @()
            missing_active_models = @($PokemonConfig.unique_models)
            classification_counts = @()
        }
    }

    $activeStems = New-StringSet
    foreach ($model in @($PokemonConfig.unique_models)) {
        [void]$activeStems.Add([string]$model.stem)
    }
    $catalogStems = New-StringSet
    foreach ($model in @($AssetCatalog.native_models)) { [void]$catalogStems.Add([string]$model.stem) }
    $authoredIds = New-StringSet
    foreach ($source in @($AssetCatalog.authored_runtime_sources)) {
        [void]$authoredIds.Add([IO.Path]::GetFileNameWithoutExtension([string]$source.source))
    }
    $environmentRoots = New-StringSet
    foreach ($environment in @($AssetCatalog.environment_resources)) {
        [void]$environmentRoots.Add([string]$environment.cooked_object_root)
    }
    $legacyCookedIds = New-StringSet
    foreach ($source in @($AssetCatalog.retained_review_sources)) {
        foreach ($identity in @($source.legacy_cooked_identities)) {
            [void]$legacyCookedIds.Add([string]$identity)
        }
    }
    $manifestScopeByDirectory = @{}
    if ($CookManifest.exists -and
        $CookManifest.schema_version -eq 2 -and
        $CookManifest.catalog_matches) {
        foreach ($entry in @($CookManifest.entries)) {
            if ([string]::IsNullOrWhiteSpace([string]$entry.object)) { continue }
            $directory = ConvertTo-PortablePath ([IO.Path]::GetDirectoryName([string]$entry.object))
            $classification = switch ([string]$entry.scope) {
                'active_gameplay' { 'active_gameplay' }
                'staged_import' { 'staged_import' }
                'authored_runtime' { 'active_authored_runtime' }
                default { 'unclassified_cooked' }
            }
            $manifestScopeByDirectory[$directory] = $classification
        }
    }

    $objects = foreach ($directory in @(Get-ChildItem -LiteralPath $objectsRoot -Directory | Sort-Object Name)) {
        $phloFiles = @(Get-ChildItem -LiteralPath $directory.FullName -File -Filter '*.phlo' | Sort-Object Name)
        $logicalIds = @($phloFiles | ForEach-Object { $_.BaseName })
        if ($logicalIds.Count -eq 0 -and $directory.Name -match '^(.*)-[0-9a-fA-F]{16}$') {
            $logicalIds = @($Matches[1])
        }
        $relativeDirectory = Get-RelativeInventoryPath $GameRoot $directory.FullName
        $classification = 'unclassified_cooked'
        if ($environmentRoots.Contains($relativeDirectory)) {
            $classification = 'environment_resource'
        } elseif ($manifestScopeByDirectory.ContainsKey($relativeDirectory)) {
            $classification = [string]$manifestScopeByDirectory[$relativeDirectory]
        } elseif (@($logicalIds | Where-Object { $legacyCookedIds.Contains([string]$_) }).Count -gt 0) {
            $classification = 'legacy_cooked_candidate'
        } elseif ($manifestScopeByDirectory.Count -gt 0 -and
            (@($logicalIds | Where-Object {
                $activeStems.Contains([string]$_) -or
                $catalogStems.Contains([string]$_) -or
                $authoredIds.Contains([string]$_)
            }).Count -gt 0)) {
            $classification = 'superseded_cooked_candidate'
        } elseif (@($logicalIds | Where-Object { $activeStems.Contains([string]$_) }).Count -gt 0) {
            $classification = 'active_gameplay'
        } elseif (@($logicalIds | Where-Object { $catalogStems.Contains([string]$_) }).Count -gt 0) {
            $classification = 'staged_import'
        } elseif (@($logicalIds | Where-Object { $authoredIds.Contains([string]$_) }).Count -gt 0) {
            $classification = 'active_authored_runtime'
        }
        $files = @(Get-ChildItem -LiteralPath $directory.FullName -Recurse -File)
        [pscustomobject][ordered]@{
            path = $relativeDirectory
            directory = $directory.Name
            logical_ids = $logicalIds
            classification = $classification
            file_count = $files.Count
            bytes = [int64](($files | Measure-Object -Property Length -Sum).Sum)
        }
    }
    $cookedLogicalIds = New-StringSet
    foreach ($item in @($objects)) {
        foreach ($logicalId in @($item.logical_ids)) {
            [void]$cookedLogicalIds.Add([string]$logicalId)
        }
    }
    $missingActiveIds = @($PokemonConfig.unique_models | Where-Object { -not $cookedLogicalIds.Contains([string]$_.stem) })

    return [pscustomobject][ordered]@{
        root = 'content/phlosion/objects'
        object_count = @($objects).Count
        objects = @($objects)
        missing_active_models = $missingActiveIds
        classification_counts = @($objects | Group-Object classification | ForEach-Object {
            [pscustomobject][ordered]@{
                classification = $_.Name
                count = $_.Count
                bytes = [int64](($_.Group.bytes | Measure-Object -Sum).Sum)
            }
        } | Sort-Object classification)
    }
}

function Get-NativePayloadDuplicates {
    param(
        [Parameter(Mandatory = $true)][string]$GameRoot,
        [switch]$Fast
    )

    $modelsRoot = Join-Path $GameRoot 'assets/models'
    if (-not (Test-Path -LiteralPath $modelsRoot -PathType Container)) {
        return [pscustomobject][ordered]@{
            mode = if ($Fast) { 'declared_hashes' } else { 'verified_sha256' }
            manifest_count = 0
            payload_count = 0
            total_bytes = [int64]0
            unique_bytes = [int64]0
            content_addressed_manifest_count = 0
            legacy_manifest_count = 0
            duplicate_byte_budget = [int64]0
            duplicate_budget_exceeded = $false
            invalid_payloads = @()
            duplicate_group_count = 0
            duplicate_file_count = 0
            redundant_bytes = [int64]0
            groups = @()
        }
    }

    $payloadRecords = @()
    $invalid = @()
    $hashCache = @{}
    foreach ($modelFile in @(Get-ChildItem -LiteralPath $modelsRoot -File -Filter '*.phmodel' | Sort-Object Name)) {
        try {
            $reader = New-Object IO.StreamReader($modelFile.FullName)
            try {
                $buffer = New-Object 'char[]' 65536
                $read = $reader.ReadBlock($buffer, 0, $buffer.Length)
                $header = New-Object string($buffer, 0, $read)
            } finally {
                $reader.Dispose()
            }
            $payloadMatch = [regex]::Match(
                $header,
                '"payload"\s*:\s*(\{[^{}]*\})',
                [Text.RegularExpressions.RegexOptions]::Singleline)
            if (-not $payloadMatch.Success) {
                throw 'missing payload object in the first 64 KiB'
            }
            $payload = $payloadMatch.Groups[1].Value | ConvertFrom-Json
            $payloadName = [string](Get-OptionalProperty $payload 'file' '')
            $declaredHash = ([string](Get-OptionalProperty $payload 'sha256' '')).ToLowerInvariant()
            $payloadPath = Join-Path $modelFile.DirectoryName $payloadName
            if ([string]::IsNullOrWhiteSpace($payloadName) -or -not (Test-Path -LiteralPath $payloadPath -PathType Leaf)) {
                throw "missing payload file '$payloadName'"
            }
            $payloadFile = Get-Item -LiteralPath $payloadPath
            $actualHash = if ($Fast) { $null } else {
                $payloadKey = $payloadFile.FullName.ToLowerInvariant()
                if (-not $hashCache.ContainsKey($payloadKey)) {
                    $hashCache[$payloadKey] =
                        (Get-FileHash -LiteralPath $payloadPath -Algorithm SHA256).Hash.ToLowerInvariant()
                }
                $hashCache[$payloadKey]
            }
            $hashMatches = if ($Fast) { $null } else { $actualHash -eq $declaredHash }
            if (-not $Fast -and -not $hashMatches) {
                $invalid += [pscustomobject][ordered]@{
                    model = Get-RelativeInventoryPath $GameRoot $modelFile.FullName
                    payload = Get-RelativeInventoryPath $GameRoot $payloadPath
                    reason = 'declared SHA-256 does not match payload bytes'
                }
            }
            $payloadRecords += [pscustomobject][ordered]@{
                model = Get-RelativeInventoryPath $GameRoot $modelFile.FullName
                payload = Get-RelativeInventoryPath $GameRoot $payloadPath
                bytes = [int64]$payloadFile.Length
                declared_sha256 = $declaredHash
                actual_sha256 = $actualHash
                hash_matches = $hashMatches
                grouping_hash = if ($Fast) { $declaredHash } else { $actualHash }
                is_content_addressed =
                    $payloadName.Replace('\', '/').TrimStart('./').Equals(
                        "_payloads/sha256/$declaredHash.bin",
                        [StringComparison]::OrdinalIgnoreCase)
            }
        } catch {
            $invalid += [pscustomobject][ordered]@{
                model = Get-RelativeInventoryPath $GameRoot $modelFile.FullName
                payload = $null
                reason = $_.Exception.Message
            }
        }
    }

    $uniquePayloadRecords = @($payloadRecords | Group-Object payload | ForEach-Object { $_.Group[0] })
    $groups = @($uniquePayloadRecords | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_.grouping_hash) } |
        Group-Object grouping_hash | Where-Object Count -gt 1 | ForEach-Object {
            $files = @($_.Group | Sort-Object payload)
            $total = [int64](($files.bytes | Measure-Object -Sum).Sum)
            $retained = [int64](($files.bytes | Measure-Object -Maximum).Maximum)
            [pscustomobject][ordered]@{
                sha256 = $_.Name
                file_count = $files.Count
                bytes_each = $retained
                redundant_bytes = $total - $retained
                files = @($files.payload)
            }
        } | Sort-Object redundant_bytes -Descending)
    $totalBytes = [int64]0
    foreach ($record in $uniquePayloadRecords) {
        $totalBytes += [int64]$record.bytes
    }
    $redundantBytes = [int64]0
    $duplicateFileCount = 0
    foreach ($group in $groups) {
        $redundantBytes += [int64]$group.redundant_bytes
        $duplicateFileCount += [int]$group.file_count
    }

    return [pscustomobject][ordered]@{
        mode = if ($Fast) { 'declared_hashes' } else { 'verified_sha256' }
        manifest_count = $payloadRecords.Count
        payload_count = $uniquePayloadRecords.Count
        total_bytes = $totalBytes
        unique_bytes = $totalBytes - $redundantBytes
        content_addressed_manifest_count = @($payloadRecords | Where-Object is_content_addressed).Count
        legacy_manifest_count = @($payloadRecords | Where-Object { -not $_.is_content_addressed }).Count
        duplicate_byte_budget = [int64]0
        duplicate_budget_exceeded = $redundantBytes -gt 0
        invalid_payloads = $invalid
        duplicate_group_count = $groups.Count
        duplicate_file_count = $duplicateFileCount
        redundant_bytes = $redundantBytes
        groups = $groups
    }
}

function Get-CookedFileDuplicates {
    param(
        [Parameter(Mandatory = $true)][string]$GameRoot,
        [switch]$Fast
    )

    $cookedRoot = Join-Path $GameRoot 'content/phlosion'
    if (-not (Test-Path -LiteralPath $cookedRoot -PathType Container)) {
        return [pscustomobject][ordered]@{
            mode = if ($Fast) { 'size_candidates_only' } else { 'verified_sha256' }
            file_count = 0
            total_bytes = [int64]0
            unique_bytes = if ($Fast) { $null } else { [int64]0 }
            duplicate_byte_budget = [int64]0
            duplicate_budget_exceeded = $false
            duplicate_group_count = 0
            duplicate_file_count = 0
            redundant_bytes = [int64]0
            candidate_group_count = 0
            redundant_bytes_upper_bound = [int64]0
            intentional_semantic_partition_bytes =
                if ($Fast) { $null } else { [int64]0 }
            unexpected_redundant_bytes =
                if ($Fast) { $null } else { [int64]0 }
            groups = @()
        }
    }

    $files = @(Get-ChildItem -LiteralPath $cookedRoot -Recurse -File | Where-Object Length -gt 0)
    if ($Fast) {
        $candidateGroups = @($files | Group-Object Length | Where-Object Count -gt 1 | ForEach-Object {
            $members = @($_.Group | Sort-Object FullName)
            [pscustomobject][ordered]@{
                candidate_key = "length:$($_.Name)"
                file_count = $members.Count
                bytes_each = [int64]$members[0].Length
                redundant_bytes_upper_bound = [int64]$members[0].Length * ($members.Count - 1)
                files = @($members | ForEach-Object { Get-RelativeInventoryPath $GameRoot $_.FullName })
            }
        } | Sort-Object redundant_bytes_upper_bound -Descending)
        return [pscustomobject][ordered]@{
            mode = 'size_candidates_only'
            file_count = $files.Count
            total_bytes = [int64](
                ($files | Measure-Object -Property Length -Sum).Sum)
            unique_bytes = $null
            duplicate_byte_budget = [int64]0
            duplicate_budget_exceeded = $null
            duplicate_group_count = $null
            duplicate_file_count = $null
            redundant_bytes = $null
            candidate_group_count = $candidateGroups.Count
            redundant_bytes_upper_bound = [int64](($candidateGroups.redundant_bytes_upper_bound | Measure-Object -Sum).Sum)
            intentional_semantic_partition_bytes = $null
            unexpected_redundant_bytes = $null
            groups = $candidateGroups
        }
    }

    $hashed = @()
    foreach ($lengthGroup in @($files | Group-Object Length | Where-Object Count -gt 1)) {
        foreach ($file in $lengthGroup.Group) {
            $hashed += [pscustomobject][ordered]@{
                path = Get-RelativeInventoryPath $GameRoot $file.FullName
                bytes = [int64]$file.Length
                sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        }
    }
    $groups = @($hashed | Group-Object sha256 | Where-Object Count -gt 1 | ForEach-Object {
        $members = @($_.Group | Sort-Object path)
        $total = [int64](($members.bytes | Measure-Object -Sum).Sum)
        $retained = [int64](($members.bytes | Measure-Object -Maximum).Maximum)
        $sharedContentIdentities = @($members.path | ForEach-Object {
            if ($_ -match '^content/phlosion/dependencies/ktx2/([0-9a-f]{16})-[0-9a-f]{16}\.ktx2$') {
                $Matches[1]
            } else { $null }
        } | Sort-Object -Unique)
        $semanticPartition = $sharedContentIdentities.Count -eq 1 -and
            -not [string]::IsNullOrWhiteSpace([string]$sharedContentIdentities[0])
        [pscustomobject][ordered]@{
            sha256 = $_.Name
            file_count = $members.Count
            bytes_each = $retained
            redundant_bytes = $total - $retained
            classification = if ($semanticPartition) { 'intentional_semantic_partition' } else { 'unexpected_duplicate' }
            extensions = @($members.path | ForEach-Object { [IO.Path]::GetExtension($_).ToLowerInvariant() } | Sort-Object -Unique)
            files = @($members.path)
        }
    } | Sort-Object redundant_bytes -Descending)

    $totalBytes = [int64](($files | Measure-Object -Property Length -Sum).Sum)
    $redundantBytes = [int64]0
    $duplicateFileCount = 0
    $intentionalSemanticBytes = [int64]0
    $unexpectedRedundantBytes = [int64]0
    foreach ($group in $groups) {
        $redundantBytes += [int64]$group.redundant_bytes
        $duplicateFileCount += [int]$group.file_count
        if ($group.classification -eq 'intentional_semantic_partition') {
            $intentionalSemanticBytes += [int64]$group.redundant_bytes
        } else {
            $unexpectedRedundantBytes += [int64]$group.redundant_bytes
        }
    }
    return [pscustomobject][ordered]@{
        mode = 'verified_sha256'
        file_count = $files.Count
        total_bytes = $totalBytes
        unique_bytes = $totalBytes - $redundantBytes
        duplicate_byte_budget = [int64]0
        duplicate_budget_exceeded = $unexpectedRedundantBytes -gt 0
        duplicate_group_count = $groups.Count
        duplicate_file_count = $duplicateFileCount
        redundant_bytes = $redundantBytes
        intentional_semantic_partition_bytes = $intentionalSemanticBytes
        unexpected_redundant_bytes = $unexpectedRedundantBytes
        groups = $groups
    }
}

function Get-CookedDependencyStoreInventory {
    param(
        [Parameter(Mandatory = $true)][string]$GameRoot,
        [Parameter(Mandatory = $true)][object]$CookManifest
    )

    $storeRoot = Join-Path $GameRoot 'content/phlosion/dependencies/ktx2'
    $physical = @()
    if (Test-Path -LiteralPath $storeRoot -PathType Container) {
        $physical = @(Get-ChildItem -LiteralPath $storeRoot -File)
    }
    $declared = New-StringSet
    $missing = @()
    $invalid = @()
    foreach ($dependency in @($CookManifest.shared_dependencies)) {
        if (-not [string]::IsNullOrWhiteSpace([string]$dependency.path)) {
            [void]$declared.Add([string]$dependency.path)
        }
        if (-not $dependency.exists) { $missing += $dependency }
        $fileName = [IO.Path]::GetFileName([string]$dependency.path)
        if ([string]$dependency.asset_id -notmatch '^dependencies/ktx2/[0-9a-f]{16}-[0-9a-f]{16}\.ktx2$' -or
            $fileName -notmatch '^([0-9a-f]{16})-[0-9a-f]{16}\.ktx2$' -or
            $Matches[1] -ne [string]$dependency.fnv1a64) {
            $invalid += $dependency
        }
    }
    $orphans = @($physical | Where-Object {
        -not $declared.Contains((Get-RelativeInventoryPath $GameRoot $_.FullName))
    } | ForEach-Object {
        [pscustomobject][ordered]@{
            path = Get-RelativeInventoryPath $GameRoot $_.FullName
            bytes = [int64]$_.Length
        }
    })
    $privateTextures = @(Get-ChildItem -LiteralPath (Join-Path $GameRoot 'content/phlosion/objects') -Recurse -Filter '*.ktx2' -File -ErrorAction SilentlyContinue)
    $physicalBytes = [int64]0
    foreach ($file in $physical) { $physicalBytes += [int64]$file.Length }
    $privateTextureBytes = [int64]0
    foreach ($file in $privateTextures) { $privateTextureBytes += [int64]$file.Length }
    return [pscustomobject][ordered]@{
        root = 'content/phlosion/dependencies/ktx2'
        manifest_count = @($CookManifest.shared_dependencies).Count
        payload_count = $physical.Count
        total_bytes = $physicalBytes
        missing_payloads = $missing
        orphan_payloads = $orphans
        invalid_identities = $invalid
        private_texture_file_count = $privateTextures.Count
        private_texture_bytes = $privateTextureBytes
        orphan_byte_budget = [int64]0
        private_texture_byte_budget = [int64]0
        budget_exceeded = $missing.Count -gt 0 -or $orphans.Count -gt 0 -or
            $invalid.Count -gt 0 -or $privateTextures.Count -gt 0
    }
}

function New-InventoryFindings {
    param([Parameter(Mandatory = $true)][object]$Inventory)

    $findings = New-Object 'System.Collections.Generic.List[object]'
    function Add-Finding([string]$Severity, [string]$Id, [string]$Message) {
        $findings.Add([pscustomobject][ordered]@{
            severity = $Severity
            id = $Id
            message = $Message
        })
    }

    if ($Inventory.pokemon_config.missing_models.Count -gt 0) {
        Add-Finding 'error' 'active-model-missing' "$($Inventory.pokemon_config.missing_models.Count) active model paths are missing."
    }
    if ($Inventory.cook_manifest.missing_sources.Count -gt 0) {
        Add-Finding 'warning' 'manifest-source-missing' "$($Inventory.cook_manifest.missing_sources.Count) cook-manifest sources are missing."
    }
    if ($Inventory.workspace.editor_ownership.stale_artifact_count -gt 0) {
        Add-Finding 'warning' 'game-local-editor-artifacts' "$($Inventory.workspace.editor_ownership.stale_artifact_count) obsolete game-local editor artifacts remain; PhlosionEditor is engine-owned."
    }
    if ($Inventory.cook_manifest.active_models_not_listed.Count -gt 0) {
        Add-Finding 'warning' 'manifest-active-model-drift' "$($Inventory.cook_manifest.active_models_not_listed.Count) active models are absent from the cook manifest."
    }
    if (-not $Inventory.cook_manifest.catalog_matches) {
        Add-Finding 'warning' 'manifest-catalog-drift' 'The cook manifest does not identify the current asset catalog and counts.'
    }
    if ($Inventory.source_assets.legacy_model_candidates.Count -gt 0) {
        Add-Finding 'review' 'legacy-model-glb-candidates' "$($Inventory.source_assets.legacy_model_candidates.Count) model GLBs have published native family alternatives and no runtime reference."
    }
    if ($Inventory.source_assets.unclassified_native_models.Count -gt 0) {
        Add-Finding 'review' 'unclassified-native-models' "$($Inventory.source_assets.unclassified_native_models.Count) native models need an active or staged catalog owner."
    }
    if ($Inventory.source_assets.unclassified_animsets.Count -gt 0) {
        Add-Finding 'review' 'unclassified-animsets' "$($Inventory.source_assets.unclassified_animsets.Count) animation sets need an active, staged, or GLB owner."
    }
    if ($Inventory.source_assets.unclassified_glbs.Count -gt 0) {
        Add-Finding 'review' 'unclassified-glbs' "$($Inventory.source_assets.unclassified_glbs.Count) GLBs need an authored-runtime or retained-review catalog owner."
    }
    if ($Inventory.cooked_objects.missing_active_models.Count -gt 0) {
        Add-Finding 'error' 'active-cooked-model-missing' "$($Inventory.cooked_objects.missing_active_models.Count) active models have no cooked object directory."
    }
    if ($Inventory.cooked_objects.classification_counts | Where-Object classification -eq 'unclassified_cooked') {
        $count = [int](($Inventory.cooked_objects.classification_counts | Where-Object classification -eq 'unclassified_cooked').count)
        Add-Finding 'review' 'unclassified-cooked-objects' "$count cooked object directories need catalog classification before pruning."
    }
    if ($Inventory.cooked_objects.classification_counts | Where-Object classification -eq 'superseded_cooked_candidate') {
        $count = [int](($Inventory.cooked_objects.classification_counts | Where-Object classification -eq 'superseded_cooked_candidate').count)
        Add-Finding 'review' 'superseded-cooked-objects' "$count cooked object directories share a catalogued identity but are not referenced by the current manifest."
    }
    if ($Inventory.duplicates.native_payloads.invalid_payloads.Count -gt 0) {
        Add-Finding 'error' 'native-payload-integrity' "$($Inventory.duplicates.native_payloads.invalid_payloads.Count) native payload declarations failed validation."
    }
    if ($Inventory.duplicates.native_payloads.duplicate_budget_exceeded) {
        Add-Finding 'warning' 'duplicate-native-payload-budget' "$($Inventory.duplicates.native_payloads.redundant_bytes) duplicate native payload bytes exceed the zero-byte budget."
    }
    if ($Inventory.duplicates.native_payloads.legacy_manifest_count -gt 0) {
        Add-Finding 'warning' 'legacy-native-payload-layout' "$($Inventory.duplicates.native_payloads.legacy_manifest_count) native manifests do not use content-addressed payload identities."
    }
    if ($Inventory.cooked_dependency_store.missing_payloads.Count -gt 0 -or
        $Inventory.cooked_dependency_store.invalid_identities.Count -gt 0) {
        Add-Finding 'error' 'cooked-dependency-integrity' 'The shared cooked dependency manifest has missing or invalid KTX2 identities.'
    }
    if ($Inventory.cooked_dependency_store.orphan_payloads.Count -gt 0) {
        Add-Finding 'warning' 'orphan-cooked-dependencies' "$($Inventory.cooked_dependency_store.orphan_payloads.Count) unreferenced shared KTX2 payloads exceed the zero-file budget."
    }
    if ($Inventory.cooked_dependency_store.private_texture_file_count -gt 0) {
        Add-Finding 'warning' 'private-cooked-textures' "$($Inventory.cooked_dependency_store.private_texture_file_count) KTX2 files remain copied inside object directories."
    }
    if ($null -ne $Inventory.duplicates.cooked_files.unexpected_redundant_bytes -and $Inventory.duplicates.cooked_files.unexpected_redundant_bytes -gt 0) {
        Add-Finding 'warning' 'duplicate-cooked-files' "$($Inventory.duplicates.cooked_files.unexpected_redundant_bytes) unexpected exact duplicate cooked bytes exceed the zero-byte budget."
    }
    return @($findings | ForEach-Object { $_ })
}

function Format-ByteCount {
    param($Bytes)

    if ($null -eq $Bytes) { return 'n/a' }
    $value = [double]$Bytes
    if ($value -ge 1GB) { return ('{0:N2} GiB' -f ($value / 1GB)) }
    if ($value -ge 1MB) { return ('{0:N2} MiB' -f ($value / 1MB)) }
    if ($value -ge 1KB) { return ('{0:N2} KiB' -f ($value / 1KB)) }
    return ('{0:N0} B' -f $value)
}

function Write-HousekeepingMarkdownReport {
    param(
        [Parameter(Mandatory = $true)][object]$Inventory,
        [Parameter(Mandatory = $true)][string]$PathValue
    )

    $builder = New-Object Text.StringBuilder
    $tick = [char]96
    [void]$builder.AppendLine('# Housekeeping Inventory')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("Generated: $($Inventory.generated_at_utc)")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('This is a read-only classification report. `review` and `candidate` do not mean safe to delete.')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('## Provenance')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('| Repository | Commit | Branch | Dirty |')
    [void]$builder.AppendLine('| --- | --- | --- | --- |')
    [void]$builder.AppendLine("| Game | $tick$($Inventory.provenance.game.commit)$tick | $($Inventory.provenance.game.branch) | $($Inventory.provenance.game.dirty) |")
    [void]$builder.AppendLine("| Engine | $tick$($Inventory.provenance.engine.commit)$tick | $($Inventory.provenance.engine.branch) | $($Inventory.provenance.engine.dirty) |")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('## Active Configuration and Recipes')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("- Pokemon: $($Inventory.pokemon_config.species_count)")
    [void]$builder.AppendLine("- Unique active model paths: $($Inventory.pokemon_config.unique_model_count)")
    [void]$builder.AppendLine("- Missing active model paths: $($Inventory.pokemon_config.missing_models.Count)")
    [void]$builder.AppendLine("- Recipe files: $($Inventory.import_recipes.recipes.Count)")
    [void]$builder.AppendLine("- Recipe outputs: $($Inventory.import_recipes.output_count)")
    [void]$builder.AppendLine("- Published recipe outputs: $($Inventory.import_recipes.published_output_count)")
    [void]$builder.AppendLine("- Unpublished recipe outputs: $($Inventory.import_recipes.unpublished_output_count)")
    [void]$builder.AppendLine("- Catalogued native models: $($Inventory.asset_catalog.native_model_count)")
    [void]$builder.AppendLine("- Catalogued authored runtime sources: $($Inventory.asset_catalog.authored_runtime_source_count)")
    [void]$builder.AppendLine("- Catalogued retained review sources: $($Inventory.asset_catalog.retained_review_source_count)")
    [void]$builder.AppendLine("- Configured headless tests: $($Inventory.workspace.ctest.test_count) ($($Inventory.workspace.ctest.note))")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('## Existing Build Artifacts')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('| Owner | Role | Configuration | Exists | Last write (UTC) | SHA-256 |')
    [void]$builder.AppendLine('| --- | --- | --- | --- | --- | --- |')
    foreach ($artifact in $Inventory.workspace.key_artifacts) {
        $shortHash = if ([string]::IsNullOrWhiteSpace([string]$artifact.sha256)) { 'not calculated' } else { ([string]$artifact.sha256).Substring(0, 12) }
        [void]$builder.AppendLine("| $($artifact.owner) | $($artifact.role) | $($artifact.configuration) | $($artifact.exists) | $($artifact.last_write_utc) | $shortHash |")
    }
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('## Editor Ownership')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("- Expected editor owner: $($Inventory.workspace.editor_ownership.expected_owner)")
    [void]$builder.AppendLine("- Game build enables editor target: $($Inventory.workspace.editor_ownership.game_build_editor_enabled)")
    [void]$builder.AppendLine("- Obsolete game-local artifacts: $($Inventory.workspace.editor_ownership.stale_artifact_count) ($(Format-ByteCount $Inventory.workspace.editor_ownership.stale_artifact_bytes))")
    foreach ($artifact in $Inventory.workspace.editor_ownership.stale_artifacts) {
        [void]$builder.AppendLine("  - $tick$($artifact.path)$tick ($($artifact.kind), $(Format-ByteCount $artifact.bytes))")
    }
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('## Cook State')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("- Manifest Pokemon entries: $($Inventory.cook_manifest.pokemon_count)")
    [void]$builder.AppendLine("- Manifest staged entries: $($Inventory.cook_manifest.staged_count)")
    [void]$builder.AppendLine("- Manifest auxiliary entries: $($Inventory.cook_manifest.auxiliary_count)")
    [void]$builder.AppendLine("- Manifest retained-review entries: $($Inventory.cook_manifest.retained_review_count)")
    [void]$builder.AppendLine("- Manifest matches asset catalog: $($Inventory.cook_manifest.catalog_matches)")
    [void]$builder.AppendLine("- Missing manifest sources: $($Inventory.cook_manifest.missing_sources.Count)")
    [void]$builder.AppendLine("- Missing manifest objects: $($Inventory.cook_manifest.missing_objects.Count)")
    [void]$builder.AppendLine("- Active models absent from manifest: $($Inventory.cook_manifest.active_models_not_listed.Count)")
    [void]$builder.AppendLine("- Cooked object directories: $($Inventory.cooked_objects.object_count)")
    [void]$builder.AppendLine("- Active models without a cooked object: $($Inventory.cooked_objects.missing_active_models.Count)")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('| Cooked classification | Count | Bytes |')
    [void]$builder.AppendLine('| --- | ---: | ---: |')
    foreach ($group in $Inventory.cooked_objects.classification_counts) {
        [void]$builder.AppendLine("| $($group.classification) | $($group.count) | $(Format-ByteCount $group.bytes) |")
    }
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('## GLB Disposition')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('| Asset | Classification | Animset | Runtime refs | Test refs | Bytes |')
    [void]$builder.AppendLine('| --- | --- | --- | ---: | ---: | ---: |')
    foreach ($glb in $Inventory.source_assets.glbs) {
        $animset = if ($null -eq $glb.companion_animset) { 'none' } else { $tick + $glb.companion_animset + $tick }
        [void]$builder.AppendLine("| $tick$($glb.path)$tick | $($glb.classification) | $animset | $($glb.runtime_reference_count) | $($glb.test_reference_count) | $(Format-ByteCount $glb.bytes) |")
    }
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('## Classification Review Queue')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('| Kind | Identity | Reason |')
    [void]$builder.AppendLine('| --- | --- | --- |')
    foreach ($model in $Inventory.source_assets.unclassified_native_models) {
        [void]$builder.AppendLine("| Native model | $tick$($model.path)$tick | No active configuration or import-recipe owner |")
    }
    foreach ($animset in $Inventory.source_assets.unclassified_animsets) {
        [void]$builder.AppendLine("| Animation set | $tick$($animset.path)$tick | No active, staged, or GLB owner |")
    }
    foreach ($object in @($Inventory.cooked_objects.objects | Where-Object classification -eq 'unclassified_cooked')) {
        [void]$builder.AppendLine("| Cooked object | $tick$($object.path)$tick | No active, staged, VFX, capture, or environment owner |")
    }
    foreach ($object in @($Inventory.cooked_objects.objects | Where-Object classification -eq 'legacy_cooked_candidate')) {
        [void]$builder.AppendLine("| Cooked object | $tick$($object.path)$tick | Legacy identity has an active native family replacement |")
    }
    foreach ($object in @($Inventory.cooked_objects.objects | Where-Object classification -eq 'superseded_cooked_candidate')) {
        [void]$builder.AppendLine("| Cooked object | $tick$($object.path)$tick | Catalogued logical identity, but not the object directory referenced by schema-2 manifest |")
    }
    foreach ($output in @($Inventory.import_recipes.outputs | Where-Object { -not $_.published })) {
        [void]$builder.AppendLine("| Recipe output | $tick$($output.stem)$tick | Declared by $($output.source_game), not published locally |")
    }
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('## Duplicate Content')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('| Surface | Mode | Groups | Files | Total bytes | Unique bytes | Redundant bytes |')
    [void]$builder.AppendLine('| --- | --- | ---: | ---: | ---: | ---: | ---: |')
    [void]$builder.AppendLine("| Native payloads | $($Inventory.duplicates.native_payloads.mode) | $($Inventory.duplicates.native_payloads.duplicate_group_count) | $($Inventory.duplicates.native_payloads.payload_count) | $(Format-ByteCount $Inventory.duplicates.native_payloads.total_bytes) | $(Format-ByteCount $Inventory.duplicates.native_payloads.unique_bytes) | $(Format-ByteCount $Inventory.duplicates.native_payloads.redundant_bytes) |")
    [void]$builder.AppendLine("| Cooked files | $($Inventory.duplicates.cooked_files.mode) | $($Inventory.duplicates.cooked_files.duplicate_group_count) | $($Inventory.duplicates.cooked_files.duplicate_file_count) | n/a | n/a | $(Format-ByteCount $Inventory.duplicates.cooked_files.redundant_bytes) |")
    [void]$builder.AppendLine()
    if ($Inventory.duplicates.cooked_files.mode -eq 'verified_sha256') {
        [void]$builder.AppendLine("Cooked redundant bytes split into $(Format-ByteCount $Inventory.duplicates.cooked_files.intentional_semantic_partition_bytes) of intentional semantic partitions and $(Format-ByteCount $Inventory.duplicates.cooked_files.unexpected_redundant_bytes) of unexpected duplication (zero-byte budget).")
        [void]$builder.AppendLine()
    }
    [void]$builder.AppendLine('## Shared Cooked Dependencies')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("- Manifest-owned KTX2 payloads: $($Inventory.cooked_dependency_store.manifest_count)")
    [void]$builder.AppendLine("- Physical KTX2 payloads: $($Inventory.cooked_dependency_store.payload_count)")
    [void]$builder.AppendLine("- Store bytes: $(Format-ByteCount $Inventory.cooked_dependency_store.total_bytes)")
    [void]$builder.AppendLine("- Missing payloads: $($Inventory.cooked_dependency_store.missing_payloads.Count)")
    [void]$builder.AppendLine("- Orphan payloads: $($Inventory.cooked_dependency_store.orphan_payloads.Count)")
    [void]$builder.AppendLine("- Invalid identities: $($Inventory.cooked_dependency_store.invalid_identities.Count)")
    [void]$builder.AppendLine("- Private object KTX2 copies: $($Inventory.cooked_dependency_store.private_texture_file_count) ($(Format-ByteCount $Inventory.cooked_dependency_store.private_texture_bytes))")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('## Workspace Directories')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('| Path | Classification | Bytes |')
    [void]$builder.AppendLine('| --- | --- | ---: |')
    foreach ($directory in $Inventory.workspace.game_directories) {
        [void]$builder.AppendLine("| $tick$($directory.path)$tick | $($directory.category) | $(Format-ByteCount $directory.bytes) |")
    }
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('## Findings')
    [void]$builder.AppendLine()
    foreach ($finding in $Inventory.findings) {
        [void]$builder.AppendLine("- **$($finding.severity)** $tick$($finding.id)$tick`: $($finding.message)")
    }
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('## Next Gate')
    [void]$builder.AppendLine()
    if (@($Inventory.findings | Where-Object { $_.severity -in @('error', 'warning') }).Count -gt 0) {
        [void]$builder.AppendLine('Repair validation errors and manifest/catalog drift before deleting any source or cooked asset.')
    } else {
        [void]$builder.AppendLine('Review the explicitly classified legacy and superseded candidates, then tackle shared native payload and cooked dependency storage without weakening source provenance.')
    }

    Set-Content -LiteralPath $PathValue -Value $builder.ToString() -Encoding UTF8
}

function New-HousekeepingInventory {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$GameRoot,
        [string]$EngineRoot,
        [switch]$Fast
    )

    $GameRoot = Resolve-FullPath $GameRoot
    if (-not (Test-Path -LiteralPath $GameRoot -PathType Container)) {
        throw "Game root not found: $GameRoot"
    }
    if (-not [string]::IsNullOrWhiteSpace($EngineRoot)) {
        $EngineRoot = Resolve-FullPath $EngineRoot
        if (-not (Test-Path -LiteralPath $EngineRoot -PathType Container)) {
            $EngineRoot = $null
        }
    }

    Write-Verbose 'Reading active Pokemon configuration.'
    $pokemonConfig = Get-PokemonConfigInventory $GameRoot
    Write-Verbose 'Reading Game Freak import recipes.'
    $recipes = Get-RecipeInventory $GameRoot
    Write-Verbose 'Reading the project asset catalog.'
    $assetCatalog = Get-AssetCatalogInventory $GameRoot $recipes
    Write-Verbose 'Scanning tracked GLB references.'
    $glbReferences = Get-TrackedGlbReferences $GameRoot
    Write-Verbose 'Classifying source assets.'
    $sourceAssets = Get-SourceAssetInventory $GameRoot $pokemonConfig $glbReferences $assetCatalog
    Write-Verbose 'Reading cook manifest and cooked object catalog.'
    $cookManifest = Get-CookManifestInventory $GameRoot $pokemonConfig $assetCatalog
    $cookedObjects = Get-CookedObjectInventory $GameRoot $pokemonConfig $assetCatalog $cookManifest
    $cookedDependencyStore = Get-CookedDependencyStoreInventory $GameRoot $cookManifest
    Write-Verbose 'Checking native payload duplication and integrity.'
    $nativeDuplicates = Get-NativePayloadDuplicates $GameRoot -Fast:$Fast
    Write-Verbose 'Checking cooked file duplication.'
    $cookedDuplicates = Get-CookedFileDuplicates $GameRoot -Fast:$Fast

    $engineGit = if ($null -ne $EngineRoot) { Get-GitSnapshot $EngineRoot } else {
        [pscustomobject][ordered]@{ available = $false; commit = $null; branch = $null; dirty = $null; status = @() }
    }
    $inventory = [pscustomobject][ordered]@{
        schema = 'pokemon-autochess-housekeeping-inventory-v1'
        generated_at_utc = [DateTime]::UtcNow.ToString('o')
        mode = if ($Fast) { 'fast' } else { 'full_sha256' }
        roots = [pscustomobject][ordered]@{
            game = ConvertTo-PortablePath $GameRoot
            engine = if ($null -ne $EngineRoot) { ConvertTo-PortablePath $EngineRoot } else { $null }
        }
        environment = [pscustomobject][ordered]@{
            powershell = $PSVersionTable.PSVersion.ToString()
            os = [Environment]::OSVersion.VersionString
        }
        provenance = [pscustomobject][ordered]@{
            game = Get-GitSnapshot $GameRoot
            engine = $engineGit
        }
        workspace = [pscustomobject][ordered]@{
            game_directories = Get-WorkspaceDirectoryInventory $GameRoot
            engine_directories = if ($null -ne $EngineRoot) { Get-WorkspaceDirectoryInventory $EngineRoot } else { @() }
            key_artifacts = Get-KeyBuildArtifacts $GameRoot $EngineRoot -IncludeHash:(-not $Fast)
            editor_ownership = Get-GameEditorOwnershipInventory $GameRoot
            ctest = Get-CtestCatalog $GameRoot
        }
        pokemon_config = $pokemonConfig
        import_recipes = $recipes
        asset_catalog = $assetCatalog
        glb_references = $glbReferences
        source_assets = $sourceAssets
        cook_manifest = $cookManifest
        cooked_objects = $cookedObjects
        cooked_dependency_store = $cookedDependencyStore
        duplicates = [pscustomobject][ordered]@{
            native_payloads = $nativeDuplicates
            cooked_files = $cookedDuplicates
        }
        findings = @()
    }
    $inventory.findings = @(New-InventoryFindings $inventory)
    return $inventory
}

function Assert-HousekeepingInventory {
    param([Parameter(Mandatory = $true)][object]$Inventory)

    if ($Inventory.schema -ne 'pokemon-autochess-housekeeping-inventory-v1') {
        throw "Unexpected housekeeping inventory schema: $($Inventory.schema)"
    }
    if ([int]$Inventory.pokemon_config.unique_model_count -ne @($Inventory.pokemon_config.unique_models).Count) {
        throw 'Pokemon unique-model count does not match its record array.'
    }
    if ([int]$Inventory.import_recipes.output_count -ne @($Inventory.import_recipes.outputs).Count) {
        throw 'Import-recipe output count does not match its record array.'
    }
    if ([int]$Inventory.asset_catalog.native_model_count -ne @($Inventory.asset_catalog.native_models).Count) {
        throw 'Asset-catalog native-model count does not match its record array.'
    }
    if ([int]$Inventory.asset_catalog.native_model_count -ne [int]$Inventory.source_assets.model_file_count) {
        throw 'Asset catalog does not account for every physical native model.'
    }
    $cataloguedGlbCount = [int]$Inventory.asset_catalog.authored_runtime_source_count +
        [int]$Inventory.asset_catalog.retained_review_source_count
    if ($cataloguedGlbCount -ne [int]$Inventory.source_assets.glb_count) {
        throw 'Asset catalog does not account for every physical GLB.'
    }
    if ([int]$Inventory.cooked_objects.object_count -ne @($Inventory.cooked_objects.objects).Count) {
        throw 'Cooked-object count does not match its record array.'
    }
    if ([int]$Inventory.cooked_dependency_store.manifest_count -ne
        [int]$Inventory.cook_manifest.shared_dependency_count) {
        throw 'Cooked dependency-store count does not match the cook manifest.'
    }
    $classifiedObjectCount = [int](($Inventory.cooked_objects.classification_counts |
        Measure-Object -Property count -Sum).Sum)
    if ($classifiedObjectCount -ne [int]$Inventory.cooked_objects.object_count) {
        throw 'Cooked-object classification counts do not cover every object exactly once.'
    }
    $nativeGroupBytes = [int64]0
    foreach ($group in @($Inventory.duplicates.native_payloads.groups)) {
        $nativeGroupBytes += [int64]$group.redundant_bytes
    }
    if ($nativeGroupBytes -ne [int64]$Inventory.duplicates.native_payloads.redundant_bytes) {
        throw 'Native duplicate-group bytes do not match the duplicate summary.'
    }
    if ($Inventory.duplicates.cooked_files.mode -eq 'verified_sha256') {
        $cookedGroupBytes = [int64]0
        foreach ($group in @($Inventory.duplicates.cooked_files.groups)) {
            $cookedGroupBytes += [int64]$group.redundant_bytes
        }
        if ($cookedGroupBytes -ne [int64]$Inventory.duplicates.cooked_files.redundant_bytes) {
            throw 'Cooked duplicate-group bytes do not match the duplicate summary.'
        }
    }
}

Export-ModuleMember -Function New-HousekeepingInventory, Assert-HousekeepingInventory, Write-HousekeepingMarkdownReport
