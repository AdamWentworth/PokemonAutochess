[CmdletBinding()]
param(
    [string]$GameRoot = '',
    [string]$CatalogPath = 'config/assets/asset_catalog.json',
    [string]$PolicyPath = 'tools/assets/kanto_model_confidence_policy.json',
    [string]$OutputDirectory = '',
    [switch]$RequireAllSelectedModels
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-FullPath([string]$PathValue) {
    return [IO.Path]::GetFullPath($PathValue).TrimEnd('\', '/')
}

function Resolve-ProjectPath(
    [string]$Root,
    [string]$RelativePath,
    [switch]$AllowMissing) {
    if ([IO.Path]::IsPathRooted($RelativePath)) {
        throw "Project path must be relative: $RelativePath"
    }
    $fullPath = Resolve-FullPath (Join-Path $Root $RelativePath)
    $rootPrefix = (Resolve-FullPath $Root) + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith(
            $rootPrefix,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Project path escapes the game root: $RelativePath"
    }
    if (-not $AllowMissing -and
        -not (Test-Path -LiteralPath $fullPath)) {
        throw "Required project path is missing: $RelativePath"
    }
    return $fullPath
}

function Normalize-ProjectPath([string]$PathValue) {
    return $PathValue.Replace('\', '/').TrimStart('./')
}

function Get-PropertyNames($Object) {
    if ($null -eq $Object) { return @() }
    return @(
        $Object.PSObject.Properties |
            ForEach-Object { [string]$_.Name } |
            Sort-Object -Unique)
}

function Get-PermutationSignature($Material) {
    $options = foreach ($property in @($Material.shader_options.PSObject.Properties)) {
        '{0}={1}' -f $property.Name, [string]$property.Value
    }
    $roles = foreach ($texture in @($Material.textures)) {
        '{0}@{1}' -f [string]$texture.role, [int]$texture.slot
    }
    return @(
        'family=' + [string]$Material.shader_family
        'transparent=' + [string][bool]$Material.is_transparent
        'options=' + (@($options | Sort-Object) -join ';')
        'roles=' + (@($roles | Sort-Object) -join ';')
    ) -join '|'
}

function Get-EvidenceClassification(
    [string]$SourceTag,
    [string]$ShaderFamily,
    [string[]]$Roles,
    [string[]]$TranslationKeys,
    [bool]$HasNativeMaterial) {
    $lossy = [Collections.Generic.List[string]]::new()
    $preserved = [Collections.Generic.List[string]]::new()

    if ($TranslationKeys -contains 'base_color_texture') {
        $preserved.Add('base_color_runtime_binding')
    }
    if ($TranslationKeys -contains 'occlusion_texture') {
        $preserved.Add('occlusion_runtime_binding')
    }
    if ($Roles -contains 'NormalMap' -or $Roles -contains 'NormalMapTex') {
        $preserved.Add('native_normal_payload')
    }
    if ($Roles -contains 'RoughnessMap') {
        $preserved.Add('authored_roughness_payload')
    }
    if ($Roles -contains 'LightTblTex') {
        $lossy.Add('light_table_not_exactly_evaluated')
    }
    if ($Roles -contains 'SphereMapTex') {
        $lossy.Add('sphere_map_not_exactly_evaluated')
    }
    if ($Roles -contains 'LocalReflectionMap') {
        $lossy.Add('local_reflection_approximated')
    }
    if ($Roles -contains 'ParallaxMap') {
        $lossy.Add('eye_parallax_or_refraction_approximated')
    }
    if ($ShaderFamily -eq 'Transparent') {
        $lossy.Add('scene_color_refraction_unavailable')
    }
    if ($ShaderFamily -eq 'FresnelEffect') {
        $lossy.Add('fresnel_probe_refraction_approximated')
    }
    if ($ShaderFamily -eq 'IkCharacter') {
        $lossy.Add('ikcharacter_program_reconstructed')
    }
    if ($SourceTag -eq 'Sword' -and
        $TranslationKeys -contains 'source_normal_texture') {
        $lossy.Add('object_space_normal_preserved_not_bound')
    }
    if ($HasNativeMaterial) {
        $preserved.Add('native_material_block')
    }

    return [pscustomobject][ordered]@{
        preserved = @($preserved | Sort-Object -Unique)
        lossy_or_unknown = @($lossy | Sort-Object -Unique)
    }
}

if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $PSScriptRoot '..\..'
}
$GameRoot = Resolve-FullPath $GameRoot
$catalogFullPath = Resolve-ProjectPath $GameRoot $CatalogPath
$policyFullPath = Resolve-ProjectPath $GameRoot $PolicyPath
$modelsRoot = Resolve-ProjectPath $GameRoot 'assets/models'
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $GameRoot 'artifacts\kanto-model-material-audit'
} elseif (-not [IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $GameRoot $OutputDirectory
}
$OutputDirectory = Resolve-FullPath $OutputDirectory
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$catalog = Get-Content -LiteralPath $catalogFullPath -Raw | ConvertFrom-Json
$policy = Get-Content -LiteralPath $policyFullPath -Raw | ConvertFrom-Json
if ([int]$catalog.schema_version -ne 1 -or
    [string]$catalog.kind -ne 'pokemon_autochess_asset_catalog') {
    throw "Unsupported asset catalog: $CatalogPath"
}
if ([string]$policy.schema -ne
    'pokemon-autochess-kanto-model-confidence-policy-v1') {
    throw "Unsupported model-confidence policy: $PolicyPath"
}

$sourcePolicies = @{}
foreach ($profile in @($policy.sourceProfiles)) {
    $tag = [string]$profile.tag
    if ([string]::IsNullOrWhiteSpace($tag) -or
        $sourcePolicies.ContainsKey($tag)) {
        throw "Invalid or duplicate confidence source tag: $tag"
    }
    $sourcePolicies[$tag] = $profile
}

$selected = [Collections.Generic.List[object]]::new()
foreach ($set in @($catalog.native_import_sets)) {
    $recipeRelativePath = Normalize-ProjectPath ([string]$set.recipe)
    $recipeFullPath = Resolve-ProjectPath $GameRoot $recipeRelativePath
    $recipe = Get-Content -LiteralPath $recipeFullPath -Raw | ConvertFrom-Json
    $matchingProfiles = @(
        $sourcePolicies.Values |
            Where-Object { [string]$_.sourceGame -eq [string]$recipe.sourceGame })
    if ($matchingProfiles.Count -ne 1) {
        throw "Recipe $recipeRelativePath has no unique confidence profile."
    }
    $tag = [string]$matchingProfiles[0].tag
    $selectedStems = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    if ([string]$set.selection -eq 'include_stems') {
        foreach ($stem in @($set.stems)) {
            [void]$selectedStems.Add([string]$stem)
        }
    } elseif ([string]$set.selection -ne 'all_outputs') {
        throw "Unsupported native import selection: $($set.selection)"
    }
    foreach ($import in @($recipe.imports)) {
        if ([int]$import.speciesId -gt 151) { continue }
        foreach ($output in @($import.outputs)) {
            $stem = [string]$output.stem
            if ([string]$set.selection -eq 'include_stems' -and
                -not $selectedStems.Contains($stem)) {
                continue
            }
            $selected.Add([pscustomobject][ordered]@{
                source_tag = $tag
                source_game = [string]$recipe.sourceGame
                recipe = $recipeRelativePath
                species_id = [int]$import.speciesId
                species_name = [string]$import.speciesName
                gender_label = [string]$import.genderLabel
                appearance = [string]$output.appearance
                stem = $stem
            })
        }
    }
}

$duplicateSelections = @($selected | Group-Object stem | Where-Object Count -ne 1)
if ($duplicateSelections.Count -gt 0) {
    throw ('Selected model stems are not unique: ' +
        (@($duplicateSelections.Name) -join ', '))
}

$missingModels = [Collections.Generic.List[string]]::new()
$modelRows = [Collections.Generic.List[object]]::new()
$materialRows = [Collections.Generic.List[object]]::new()
foreach ($selection in @($selected | Sort-Object species_id, stem)) {
    $modelPath = Join-Path $modelsRoot ($selection.stem + '.phmodel')
    if (-not (Test-Path -LiteralPath $modelPath -PathType Leaf)) {
        $missingModels.Add($selection.stem)
        continue
    }
    $document = Get-Content -LiteralPath $modelPath -Raw | ConvertFrom-Json
    if ([string]$document.schema -ne 'phlosion-native-model-ir-v1' -or
        [int]$document.schema_version -ne 1) {
        throw "Unsupported native model manifest: $modelPath"
    }
    $sourceProfile = [string]$document.source.profile
    if ($sourceProfile -ne [string]$selection.source_game) {
        throw "Source profile mismatch for $($selection.stem): $sourceProfile"
    }

    $families = [Collections.Generic.List[string]]::new()
    $roles = [Collections.Generic.List[string]]::new()
    $nativeFormats = @($document.source.native_formats | ForEach-Object { [string]$_ })
    foreach ($material in @($document.materials)) {
        $family = [string]$material.shader_family
        $families.Add($family)
        $materialRoles = @($material.textures | ForEach-Object { [string]$_.role })
        foreach ($role in $materialRoles) { $roles.Add($role) }
        $translationKeys = Get-PropertyNames $material.runtime_translation
        $optionNames = Get-PropertyNames $material.shader_options
        $floatNames = Get-PropertyNames $material.float_parameters
        $vecNames = @(
            (Get-PropertyNames $material.vec2_parameters)
            (Get-PropertyNames $material.vec3_parameters)
            (Get-PropertyNames $material.vec4_parameters)
        ) | Sort-Object -Unique
        $hasNativeMaterial =
            $material.PSObject.Properties.Name -contains 'native_material'
        $classification = Get-EvidenceClassification `
            -SourceTag $selection.source_tag `
            -ShaderFamily $family `
            -Roles $materialRoles `
            -TranslationKeys $translationKeys `
            -HasNativeMaterial $hasNativeMaterial
        $signature = Get-PermutationSignature $material
        $signatureBytes = [Text.Encoding]::UTF8.GetBytes($signature)
        $sha = [Security.Cryptography.SHA256]::Create()
        try {
            $signatureHash = ([BitConverter]::ToString(
                $sha.ComputeHash($signatureBytes))).Replace('-', '').ToLowerInvariant()
        } finally {
            $sha.Dispose()
        }
        $materialRows.Add([pscustomobject][ordered]@{
            model = $selection.stem
            species_id = $selection.species_id
            species_name = $selection.species_name
            source_tag = $selection.source_tag
            material = [string]$material.name
            shader_family = $family
            transparent = [bool]$material.is_transparent
            permutation_sha256 = $signatureHash
            shader_options = @($optionNames)
            float_parameters = @($floatNames)
            vector_parameters = @($vecNames)
            texture_roles = @($materialRoles | Sort-Object -Unique)
            runtime_translation_keys = @($translationKeys)
            has_native_material_block = $hasNativeMaterial
            preserved_evidence = @($classification.preserved)
            lossy_or_unknown = @($classification.lossy_or_unknown)
        })
    }
    $animations = @($document.animations)
    $modelRows.Add([pscustomobject][ordered]@{
        model = $selection.stem
        species_id = $selection.species_id
        species_name = $selection.species_name
        source_tag = $selection.source_tag
        source_game = $selection.source_game
        recipe = $selection.recipe
        gender_label = $selection.gender_label
        appearance = $selection.appearance
        native_formats = @($nativeFormats)
        vertex_count = [int64]$document.model.vertex_count
        index_count = [int64]$document.model.index_count
        submesh_count = [int]$document.model.submesh_count
        bone_count = @($document.skeleton.bones).Count
        animation_count = $animations.Count
        material_count = @($document.materials).Count
        shader_families = @($families | Sort-Object -Unique)
        texture_roles = @($roles | Sort-Object -Unique)
        has_mesh_visibility = @(
            $animations | Where-Object { @($_.mesh_visibility).Count -gt 0 }).Count -gt 0
        has_material_animation = @(
            $animations | Where-Object { @($_.material_parameters).Count -gt 0 }).Count -gt 0
    })
}

if ($RequireAllSelectedModels -and $missingModels.Count -gt 0) {
    throw ('Selected native model manifests are missing: ' +
        (@($missingModels) -join ', '))
}

$sourceRows = [Collections.Generic.List[object]]::new()
foreach ($profile in @($policy.sourceProfiles)) {
    $tag = [string]$profile.tag
    $sourceModels = @($modelRows | Where-Object source_tag -eq $tag)
    $sourceMaterials = @($materialRows | Where-Object source_tag -eq $tag)
    $sourceRows.Add([pscustomobject][ordered]@{
        source_tag = $tag
        source_game = [string]$profile.sourceGame
        current_overall_confidence = [int]$profile.currentOverall
        target_overall_confidence = [int]$profile.targetOverall
        selected_species = @($sourceModels | Select-Object species_id -Unique).Count
        selected_models = $sourceModels.Count
        materials = $sourceMaterials.Count
        shader_families = @($sourceMaterials.shader_family | Sort-Object -Unique)
        shader_permutations = @(
            $sourceMaterials.permutation_sha256 | Sort-Object -Unique).Count
        texture_roles = @($sourceMaterials.texture_roles |
            ForEach-Object { $_ } | Sort-Object -Unique)
        known_unknowns = @($profile.knownUnknowns)
        ratings = $profile.ratings
    })
}

$captureRows = [Collections.Generic.List[object]]::new()
foreach ($target in @($policy.captureTargets | Sort-Object priority, id)) {
    foreach ($model in @($target.models)) {
        $isAvailable = Test-Path -LiteralPath (
            Join-Path $modelsRoot ([string]$model.stem + '.phmodel')) -PathType Leaf
        if ([bool]$model.requiredLocal -and -not $isAvailable) {
            throw "Required capture canary is missing: $($model.stem)"
        }
        $captureRows.Add([pscustomobject][ordered]@{
            target_id = [string]$target.id
            priority = [int]$target.priority
            source_tag = [string]$target.sourceTag
            status = [string]$target.status
            features = @($target.features | ForEach-Object { [string]$_ })
            model = [string]$model.stem
            role = [string]$model.role
            required_local = [bool]$model.requiredLocal
            locally_available = $isAvailable
        })
    }
}

$summary = [pscustomobject][ordered]@{
    selected_species = @(
        $modelRows |
            Select-Object species_id, species_name -Unique).Count
    selected_models_expected = $selected.Count
    selected_models_audited = $modelRows.Count
    selected_models_missing = $missingModels.Count
    materials = $materialRows.Count
    shader_families = @($materialRows.shader_family | Sort-Object -Unique).Count
    shader_permutations = @(
        $materialRows.permutation_sha256 | Sort-Object -Unique).Count
    capture_targets = @($policy.captureTargets).Count
    capture_canaries = $captureRows.Count
    locally_available_canaries = @(
        $captureRows | Where-Object locally_available).Count
}

$report = [pscustomobject][ordered]@{
    schema = 'pokemon-autochess-kanto-model-material-audit-v1'
    generated_utc = [DateTime]::UtcNow.ToString('o')
    catalog = Normalize-ProjectPath $CatalogPath
    confidence_policy = Normalize-ProjectPath $PolicyPath
    summary = $summary
    sources = @($sourceRows)
    models = @($modelRows)
    materials = @($materialRows)
    capture_targets = @($captureRows)
    required_capture_evidence = @($policy.requiredCaptureEvidence)
    comparison_states = @($policy.comparisonStates)
    missing_models = @($missingModels)
}

$jsonPath = Join-Path $OutputDirectory 'kanto_model_material_inventory.json'
$report | ConvertTo-Json -Depth 14 |
    Set-Content -LiteralPath $jsonPath -Encoding utf8

$markdown = [Collections.Generic.List[string]]::new()
$markdown.Add('# Kanto Model Material Inventory')
$markdown.Add('')
$markdown.Add(('Generated: `{0}`' -f $report.generated_utc))
$markdown.Add('')
$markdown.Add('Confidence values are engineering assessments from the checked-in policy, not measured pixel-similarity percentages. Counts are derived from the selected asset catalog and local canonical manifests.')
$markdown.Add('')
$markdown.Add('## Summary')
$markdown.Add('')
$markdown.Add('| Species | Expected models | Audited models | Materials | Shader families | Permutations | Capture canaries |')
$markdown.Add('| ---: | ---: | ---: | ---: | ---: | ---: | ---: |')
$markdown.Add(('| {0} | {1} | {2} | {3} | {4} | {5} | {6} |' -f
    $summary.selected_species,
    $summary.selected_models_expected,
    $summary.selected_models_audited,
    $summary.materials,
    $summary.shader_families,
    $summary.shader_permutations,
    $summary.capture_canaries))
$markdown.Add('')
$markdown.Add('## Source Profiles')
$markdown.Add('')
$markdown.Add('| Source | Species | Models | Materials | Families | Permutations | Current | Target |')
$markdown.Add('| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: |')
foreach ($source in @($sourceRows)) {
    $markdown.Add(('| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} |' -f
        $source.source_tag,
        $source.selected_species,
        $source.selected_models,
        $source.materials,
        (@($source.shader_families) -join ', '),
        $source.shader_permutations,
        $source.current_overall_confidence,
        $source.target_overall_confidence))
}
$markdown.Add('')
$markdown.Add('## Known Unknowns')
$markdown.Add('')
$markdown.Add('| Source | Severity | ID | Evidence | Summary |')
$markdown.Add('| --- | --- | --- | --- | --- |')
foreach ($source in @($sourceRows)) {
    foreach ($unknown in @($source.known_unknowns)) {
        $markdown.Add(('| {0} | {1} | {2} | {3} | {4} |' -f
            $source.source_tag,
            [string]$unknown.severity,
            [string]$unknown.id,
            [string]$unknown.evidence,
            ([string]$unknown.summary).Replace('|', '\|')))
    }
}
$markdown.Add('')
$markdown.Add('## Capture Queue')
$markdown.Add('')
$markdown.Add('| Priority | Target | Source | Model | Available | Role |')
$markdown.Add('| ---: | --- | --- | --- | --- | --- |')
foreach ($capture in @($captureRows)) {
    $markdown.Add(('| {0} | {1} | {2} | {3} | {4} | {5} |' -f
        $capture.priority,
        $capture.target_id,
        $capture.source_tag,
        $capture.model,
        $capture.locally_available,
        ([string]$capture.role).Replace('|', '\|')))
}
$markdownPath = Join-Path $OutputDirectory 'kanto_model_material_inventory.md'
$markdown | Set-Content -LiteralPath $markdownPath -Encoding utf8

Write-Host (
    'Kanto model material audit complete: {0}/{1} models, {2} materials, {3} shader families, {4} permutations, {5} missing.' -f
        $summary.selected_models_audited,
        $summary.selected_models_expected,
        $summary.materials,
        $summary.shader_families,
        $summary.shader_permutations,
        $summary.selected_models_missing)
Write-Host "JSON: $jsonPath"
Write-Host "Markdown: $markdownPath"

[pscustomobject]@{
    JsonPath = $jsonPath
    MarkdownPath = $markdownPath
    Summary = $summary
}
