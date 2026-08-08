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

    $recipeRoot = Join-Path $GameRoot 'tools/assets'
    $recipeFiles = @(Get-ChildItem -LiteralPath $recipeRoot -File -Filter 'gamefreak_pokemon_imports*.json' |
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
        [Parameter(Mandatory = $true)][object]$Recipes,
        [Parameter(Mandatory = $true)][object[]]$GlbReferences
    )

    $modelsRoot = Join-Path $GameRoot 'assets/models'
    $meshesRoot = Join-Path $GameRoot 'assets/meshes'
    $activeStems = New-StringSet
    $activeSpeciesIds = New-StringSet
    foreach ($model in @($PokemonConfig.unique_models)) {
        [void]$activeStems.Add([string]$model.stem)
        if ([string]$model.stem -match '^(\d{4})_') {
            [void]$activeSpeciesIds.Add($Matches[1])
        }
    }
    $recipeStems = New-StringSet
    $publishedRecipeSpeciesIds = New-StringSet
    foreach ($output in @($Recipes.outputs)) {
        [void]$recipeStems.Add([string]$output.stem)
        if ($output.published -and [int]$output.species_id -gt 0) {
            [void]$publishedRecipeSpeciesIds.Add(('{0:D4}' -f [int]$output.species_id))
        }
    }

    $modelFiles = if (Test-Path -LiteralPath $modelsRoot) {
        @(Get-ChildItem -LiteralPath $modelsRoot -File | Sort-Object Name)
    } else { @() }
    $nativeModels = @($modelFiles | Where-Object Extension -eq '.phmodel' | ForEach-Object {
        $stem = $_.BaseName
        $classification = if ($activeStems.Contains($stem)) { 'active_gameplay' }
            elseif ($recipeStems.Contains($stem)) { 'staged_import' }
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
            elseif ($recipeStems.Contains($stem)) { 'staged_import' }
            elseif (Test-Path -LiteralPath (Join-Path $modelsRoot ($stem + '.glb')) -PathType Leaf) { 'legacy_glb_companion' }
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
        $speciesId = $null
        if ($file.BaseName -match '^(\d{4})_') {
            $speciesId = $Matches[1]
        }
        $classification = if ($relative -eq 'assets/models/pokeball.glb') {
            'active_capture_source'
        } elseif ($relative.StartsWith('assets/meshes/')) {
            'active_authored_vfx_source'
        } elseif ($runtimeReferences.Count -gt 0) {
            'runtime_referenced_interchange'
        } elseif ($null -ne $speciesId -and $activeSpeciesIds.Contains([string]$speciesId)) {
            'legacy_model_candidate'
        } elseif ($null -ne $speciesId -and $publishedRecipeSpeciesIds.Contains([string]$speciesId)) {
            'legacy_model_candidate_staged'
        } else {
            'staged_model_source_review'
        }
        [pscustomobject][ordered]@{
            path = $relative
            stem = $file.BaseName
            bytes = [int64]$file.Length
            classification = $classification
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
        legacy_glb_animsets = @($animsets | Where-Object classification -eq 'legacy_glb_companion')
        legacy_model_candidates = @($glbs | Where-Object { $_.classification -like 'legacy_model_candidate*' })
    }
}

function Get-CookManifestInventory {
    param(
        [Parameter(Mandatory = $true)][string]$GameRoot,
        [Parameter(Mandatory = $true)][object]$PokemonConfig
    )

    $relativePath = 'content/phlosion/cook_manifest.json'
    $fullPath = Join-Path $GameRoot ($relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        return [pscustomobject][ordered]@{
            path = $relativePath
            exists = $false
            kind = $null
            pokemon_count = 0
            auxiliary_count = 0
            missing_sources = @()
            missing_objects = @()
            active_models_not_listed = @($PokemonConfig.unique_models.model)
            entries = @()
        }
    }

    $document = Get-Content -LiteralPath $fullPath -Raw | ConvertFrom-Json
    $pokemonEntries = @(Get-OptionalProperty $document 'pokemon' @())
    $auxiliaryEntries = @(Get-OptionalProperty $document 'runtime_auxiliary_objects' @())
    $entries = @()
    foreach ($entry in @($pokemonEntries + $auxiliaryEntries)) {
        $source = ConvertTo-PortablePath ([string](Get-OptionalProperty $entry 'source' ''))
        $object = ConvertTo-PortablePath ([string](Get-OptionalProperty $entry 'object' ''))
        $sourceFullPath = Join-Path $GameRoot $source.Replace('/', [IO.Path]::DirectorySeparatorChar)
        $objectFullPath = Join-Path $GameRoot $object.Replace('/', [IO.Path]::DirectorySeparatorChar)
        $entries += [pscustomobject][ordered]@{
            source = $source
            object = $object
            source_exists = -not [string]::IsNullOrWhiteSpace($source) -and (Test-Path -LiteralPath $sourceFullPath -PathType Leaf)
            object_exists = -not [string]::IsNullOrWhiteSpace($object) -and (Test-Path -LiteralPath $objectFullPath -PathType Leaf)
        }
    }
    $listedSourceNames = New-StringSet
    foreach ($entry in $entries) {
        [void]$listedSourceNames.Add([IO.Path]::GetFileName([string]$entry.source))
    }
    $activeNotListed = @($PokemonConfig.unique_models | Where-Object { -not $listedSourceNames.Contains([string]$_.model) })

    return [pscustomobject][ordered]@{
        path = $relativePath
        exists = $true
        kind = [string](Get-OptionalProperty $document 'kind' '')
        pokemon_count = $pokemonEntries.Count
        auxiliary_count = $auxiliaryEntries.Count
        missing_sources = @($entries | Where-Object { -not $_.source_exists })
        missing_objects = @($entries | Where-Object { -not $_.object_exists })
        active_models_not_listed = $activeNotListed
        entries = $entries
    }
}

function Get-CookedObjectInventory {
    param(
        [Parameter(Mandatory = $true)][string]$GameRoot,
        [Parameter(Mandatory = $true)][object]$PokemonConfig,
        [Parameter(Mandatory = $true)][object]$Recipes
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
    $activeSpeciesIds = New-StringSet
    foreach ($model in @($PokemonConfig.unique_models)) {
        [void]$activeStems.Add([string]$model.stem)
        if ([string]$model.stem -match '^(\d{4})_') {
            [void]$activeSpeciesIds.Add($Matches[1])
        }
    }
    $recipeStems = New-StringSet
    foreach ($output in @($Recipes.outputs)) { [void]$recipeStems.Add([string]$output.stem) }

    $objects = foreach ($directory in @(Get-ChildItem -LiteralPath $objectsRoot -Directory | Sort-Object Name)) {
        $phloFiles = @(Get-ChildItem -LiteralPath $directory.FullName -File -Filter '*.phlo' | Sort-Object Name)
        $logicalIds = @($phloFiles | ForEach-Object { $_.BaseName })
        if ($logicalIds.Count -eq 0 -and $directory.Name -match '^(.*)-[0-9a-fA-F]{16}$') {
            $logicalIds = @($Matches[1])
        }
        $classification = 'unclassified_cooked'
        if ($directory.Name -eq 'environment') {
            $classification = 'environment_resource'
        } elseif (@($logicalIds | Where-Object { $activeStems.Contains([string]$_) }).Count -gt 0) {
            $classification = 'active_gameplay'
        } elseif (@($logicalIds | Where-Object { $recipeStems.Contains([string]$_) }).Count -gt 0) {
            $classification = 'staged_import'
        } elseif (@($logicalIds | Where-Object { $_ -eq 'pokeball' }).Count -gt 0) {
            $classification = 'active_capture'
        } elseif (@($logicalIds | Where-Object { $_ -like 'growl_*' }).Count -gt 0) {
            $classification = 'active_authored_vfx'
        } elseif (@($logicalIds | Where-Object {
            [string]$_ -match '^(\d{4})_' -and $activeSpeciesIds.Contains($Matches[1])
        }).Count -gt 0) {
            $classification = 'legacy_cooked_candidate'
        }
        $files = @(Get-ChildItem -LiteralPath $directory.FullName -Recurse -File)
        [pscustomobject][ordered]@{
            path = Get-RelativeInventoryPath $GameRoot $directory.FullName
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
            payload_count = 0
            invalid_payloads = @()
            duplicate_group_count = 0
            duplicate_file_count = 0
            redundant_bytes = [int64]0
            groups = @()
        }
    }

    $payloadRecords = @()
    $invalid = @()
    foreach ($modelFile in @(Get-ChildItem -LiteralPath $modelsRoot -File -Filter '*.phmodel' | Sort-Object Name)) {
        try {
            $document = Get-Content -LiteralPath $modelFile.FullName -Raw | ConvertFrom-Json
            $payload = Get-OptionalProperty $document 'payload'
            if ($null -eq $payload) {
                throw 'missing payload object'
            }
            $payloadName = [string](Get-OptionalProperty $payload 'file' '')
            $declaredHash = ([string](Get-OptionalProperty $payload 'sha256' '')).ToLowerInvariant()
            $payloadPath = Join-Path $modelFile.DirectoryName $payloadName
            if ([string]::IsNullOrWhiteSpace($payloadName) -or -not (Test-Path -LiteralPath $payloadPath -PathType Leaf)) {
                throw "missing payload file '$payloadName'"
            }
            $payloadFile = Get-Item -LiteralPath $payloadPath
            $actualHash = if ($Fast) { $null } else {
                (Get-FileHash -LiteralPath $payloadPath -Algorithm SHA256).Hash.ToLowerInvariant()
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

    return [pscustomobject][ordered]@{
        mode = if ($Fast) { 'declared_hashes' } else { 'verified_sha256' }
        payload_count = $uniquePayloadRecords.Count
        invalid_payloads = $invalid
        duplicate_group_count = $groups.Count
        duplicate_file_count = [int](($groups.file_count | Measure-Object -Sum).Sum)
        redundant_bytes = [int64](($groups.redundant_bytes | Measure-Object -Sum).Sum)
        groups = $groups
    }
}

function Get-CookedFileDuplicates {
    param(
        [Parameter(Mandatory = $true)][string]$GameRoot,
        [switch]$Fast
    )

    $objectsRoot = Join-Path $GameRoot 'content/phlosion/objects'
    if (-not (Test-Path -LiteralPath $objectsRoot -PathType Container)) {
        return [pscustomobject][ordered]@{
            mode = if ($Fast) { 'size_candidates_only' } else { 'verified_sha256' }
            file_count = 0
            duplicate_group_count = 0
            duplicate_file_count = 0
            redundant_bytes = [int64]0
            groups = @()
        }
    }

    $files = @(Get-ChildItem -LiteralPath $objectsRoot -Recurse -File | Where-Object Length -gt 0)
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
            duplicate_group_count = $null
            duplicate_file_count = $null
            redundant_bytes = $null
            candidate_group_count = $candidateGroups.Count
            redundant_bytes_upper_bound = [int64](($candidateGroups.redundant_bytes_upper_bound | Measure-Object -Sum).Sum)
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
        [pscustomobject][ordered]@{
            sha256 = $_.Name
            file_count = $members.Count
            bytes_each = $retained
            redundant_bytes = $total - $retained
            extensions = @($members.path | ForEach-Object { [IO.Path]::GetExtension($_).ToLowerInvariant() } | Sort-Object -Unique)
            files = @($members.path)
        }
    } | Sort-Object redundant_bytes -Descending)

    return [pscustomobject][ordered]@{
        mode = 'verified_sha256'
        file_count = $files.Count
        duplicate_group_count = $groups.Count
        duplicate_file_count = [int](($groups.file_count | Measure-Object -Sum).Sum)
        redundant_bytes = [int64](($groups.redundant_bytes | Measure-Object -Sum).Sum)
        groups = $groups
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
    if ($Inventory.cook_manifest.active_models_not_listed.Count -gt 0) {
        Add-Finding 'warning' 'manifest-active-model-drift' "$($Inventory.cook_manifest.active_models_not_listed.Count) active models are absent from the cook manifest."
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
    if ($Inventory.cooked_objects.missing_active_models.Count -gt 0) {
        Add-Finding 'error' 'active-cooked-model-missing' "$($Inventory.cooked_objects.missing_active_models.Count) active models have no cooked object directory."
    }
    if ($Inventory.cooked_objects.classification_counts | Where-Object classification -eq 'unclassified_cooked') {
        $count = [int](($Inventory.cooked_objects.classification_counts | Where-Object classification -eq 'unclassified_cooked').count)
        Add-Finding 'review' 'unclassified-cooked-objects' "$count cooked object directories need catalog classification before pruning."
    }
    if ($Inventory.duplicates.native_payloads.invalid_payloads.Count -gt 0) {
        Add-Finding 'error' 'native-payload-integrity' "$($Inventory.duplicates.native_payloads.invalid_payloads.Count) native payload declarations failed validation."
    }
    if ($null -ne $Inventory.duplicates.native_payloads.redundant_bytes -and $Inventory.duplicates.native_payloads.redundant_bytes -gt 0) {
        Add-Finding 'opportunity' 'duplicate-native-payloads' "$($Inventory.duplicates.native_payloads.redundant_bytes) duplicate native payload bytes are recoverable through shared identities."
    }
    if ($null -ne $Inventory.duplicates.cooked_files.redundant_bytes -and $Inventory.duplicates.cooked_files.redundant_bytes -gt 0) {
        Add-Finding 'opportunity' 'duplicate-cooked-files' "$($Inventory.duplicates.cooked_files.redundant_bytes) exact duplicate cooked bytes are recoverable through shared dependencies."
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
    [void]$builder.AppendLine('## Cook State')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("- Manifest Pokemon entries: $($Inventory.cook_manifest.pokemon_count)")
    [void]$builder.AppendLine("- Manifest auxiliary entries: $($Inventory.cook_manifest.auxiliary_count)")
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
    foreach ($output in @($Inventory.import_recipes.outputs | Where-Object { -not $_.published })) {
        [void]$builder.AppendLine("| Recipe output | $tick$($output.stem)$tick | Declared by $($output.source_game), not published locally |")
    }
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('## Duplicate Content')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('| Surface | Mode | Groups | Files | Redundant bytes |')
    [void]$builder.AppendLine('| --- | --- | ---: | ---: | ---: |')
    [void]$builder.AppendLine("| Native payloads | $($Inventory.duplicates.native_payloads.mode) | $($Inventory.duplicates.native_payloads.duplicate_group_count) | $($Inventory.duplicates.native_payloads.duplicate_file_count) | $(Format-ByteCount $Inventory.duplicates.native_payloads.redundant_bytes) |")
    [void]$builder.AppendLine("| Cooked files | $($Inventory.duplicates.cooked_files.mode) | $($Inventory.duplicates.cooked_files.duplicate_group_count) | $($Inventory.duplicates.cooked_files.duplicate_file_count) | $(Format-ByteCount $Inventory.duplicates.cooked_files.redundant_bytes) |")
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
    [void]$builder.AppendLine('Repair and validate the cook manifest and staged-asset catalog before deleting any source or cooked asset.')

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
    Write-Verbose 'Scanning tracked GLB references.'
    $glbReferences = Get-TrackedGlbReferences $GameRoot
    Write-Verbose 'Classifying source assets.'
    $sourceAssets = Get-SourceAssetInventory $GameRoot $pokemonConfig $recipes $glbReferences
    Write-Verbose 'Reading cook manifest and cooked object catalog.'
    $cookManifest = Get-CookManifestInventory $GameRoot $pokemonConfig
    $cookedObjects = Get-CookedObjectInventory $GameRoot $pokemonConfig $recipes
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
            ctest = Get-CtestCatalog $GameRoot
        }
        pokemon_config = $pokemonConfig
        import_recipes = $recipes
        glb_references = $glbReferences
        source_assets = $sourceAssets
        cook_manifest = $cookManifest
        cooked_objects = $cookedObjects
        duplicates = [pscustomobject][ordered]@{
            native_payloads = $nativeDuplicates
            cooked_files = $cookedDuplicates
        }
        findings = @()
    }
    $inventory.findings = New-InventoryFindings $inventory
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
    if ([int]$Inventory.cooked_objects.object_count -ne @($Inventory.cooked_objects.objects).Count) {
        throw 'Cooked-object count does not match its record array.'
    }
    $classifiedObjectCount = [int](($Inventory.cooked_objects.classification_counts |
        Measure-Object -Property count -Sum).Sum)
    if ($classifiedObjectCount -ne [int]$Inventory.cooked_objects.object_count) {
        throw 'Cooked-object classification counts do not cover every object exactly once.'
    }
    $nativeGroupBytes = [int64](($Inventory.duplicates.native_payloads.groups |
        Measure-Object -Property redundant_bytes -Sum).Sum)
    if ($nativeGroupBytes -ne [int64]$Inventory.duplicates.native_payloads.redundant_bytes) {
        throw 'Native duplicate-group bytes do not match the duplicate summary.'
    }
    if ($Inventory.duplicates.cooked_files.mode -eq 'verified_sha256') {
        $cookedGroupBytes = [int64](($Inventory.duplicates.cooked_files.groups |
            Measure-Object -Property redundant_bytes -Sum).Sum)
        if ($cookedGroupBytes -ne [int64]$Inventory.duplicates.cooked_files.redundant_bytes) {
            throw 'Cooked duplicate-group bytes do not match the duplicate summary.'
        }
    }
}

Export-ModuleMember -Function New-HousekeepingInventory, Assert-HousekeepingInventory, Write-HousekeepingMarkdownReport
