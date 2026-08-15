[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ReportPath,
    [string]$GameRoot = '',
    [string]$PromotedReportPath = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Resolve-InputPath([string]$Root, [string]$Value) {
    if ([IO.Path]::IsPathRooted($Value)) {
        return [IO.Path]::GetFullPath($Value)
    }
    return [IO.Path]::GetFullPath((Join-Path $Root $Value))
}

function Assert-Sha256([string]$Value, [string]$Label) {
    Assert-Condition ($Value -match '^[a-fA-F0-9]{64}$') (
        "$Label is not a SHA-256 digest.")
}

if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $PSScriptRoot '..\..'
}
$GameRoot = [IO.Path]::GetFullPath($GameRoot)
$ReportPath = Resolve-InputPath $GameRoot $ReportPath
Assert-Condition (Test-Path -LiteralPath $ReportPath -PathType Leaf) (
    "Static material report is missing: $ReportPath")
$report = Get-Content -LiteralPath $ReportPath -Raw | ConvertFrom-Json

Assert-Condition ([string]$report.schema -eq
    'pokemon-autochess-static-character-material-report-v1') (
    'Unsupported static character-material report schema.')
Assert-Condition ([int]$report.subject.species_id -eq 133 -and
    [string]$report.subject.species_name -eq 'Eevee' -and
    [string]$report.subject.source_profile -eq 'pokemon-scarlet-v3.0.1') (
    'Static report subject is not Scarlet 3.0.1 Eevee.')
Assert-Condition (-not [bool]$report.method.runtime_execution -and
    -not [bool]$report.method.emulator_used) (
    'Static report unexpectedly claims runtime/emulator execution.')

$manifestRelative = [string]$report.subject.canonical_manifest
Assert-Condition (-not [IO.Path]::IsPathRooted($manifestRelative)) (
    'Canonical manifest identity must be repository-relative.')
$manifestPath = Resolve-InputPath $GameRoot $manifestRelative
Assert-Condition (Test-Path -LiteralPath $manifestPath -PathType Leaf) (
    "Canonical manifest is missing: $manifestRelative")
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
Assert-Condition ([string]$manifest.source.model_sha256 -eq
    [string]$report.subject.source_model_sha256) (
    'Static report source-model hash no longer matches the manifest.')
Assert-Condition ([string]$manifest.payload.sha256 -eq
    [string]$report.subject.payload_sha256) (
    'Static report payload hash no longer matches the manifest.')

$expectedPrograms = @{
    'SSS' = @{ Variation = 56; Shader = '0x41F'; Global = '0x1' }
    'EyeClearCoat' = @{ Variation = 20; Shader = '0x24'; Global = '0x0' }
}
Assert-Condition (@($report.shader_evidence).Count -eq 2) (
    'Static report must contain exactly SSS and EyeClearCoat shader evidence.')
foreach ($program in @($report.shader_evidence)) {
    $family = [string]$program.family
    Assert-Condition $expectedPrograms.ContainsKey($family) (
        "Unexpected shader family in static report: $family")
    $expected = $expectedPrograms[$family]
    Assert-Condition ([int]$program.resolved_variation -eq
        [int]$expected.Variation) (
        "$family resolved to the wrong shader variation.")
    Assert-Sha256 ([string]$program.archive.sha256) "$family archive"
    $reflection = $program.archive.reflection
    Assert-Condition ([string]$reflection.status -eq 'absent_or_stripped' -and
        [int]$reflection.variation_index -eq [int]$expected.Variation -and
        [string]$reflection.reflection_pointer_hex -eq '0x0' -and
        [string]$reflection.object_offset_hex -ne '0x0') (
        "$family shipped BNSH reflection boundary changed.")
    Assert-Sha256 ([string]$program.metadata.sha256) "$family metadata"
    foreach ($key in @($program.material_keys)) {
        Assert-Condition ([string]$key.shader_key_hex -eq
            [string]$expected.Shader -and
            [string]$key.global_key_hex -eq [string]$expected.Global) (
            "$family material key does not match the expected static mapping.")
    }
    $fragment = @($program.program_files |
        Where-Object kind -eq 'fsh.maxwell.glsl')
    Assert-Condition ($fragment.Count -eq 1) (
        "$family is missing its offline fragment decompilation identity.")
    Assert-Sha256 ([string]$fragment[0].sha256) "$family fragment GLSL"
}

$bindingRows = @($report.binding_differentials)
Assert-Condition ($bindingRows.Count -eq 2) (
    'Static report must contain SSS and EyeClearCoat binding differentials.')
$sssBindings = @($bindingRows | Where-Object family -eq 'SSS')
Assert-Condition ($sssBindings.Count -eq 1 -and
    [string]$sssBindings[0].status -eq 'exact_material_texture_bindings') (
    'SSS binding differential is missing or incomplete.')
Assert-Condition (@($sssBindings[0].permutations).Count -eq 5) (
    'SSS binding differential must contain five compiled permutations.')
foreach ($permutation in @($sssBindings[0].permutations)) {
    Assert-Sha256 ([string]$permutation.fragment_glsl_sha256) (
        "SSS variation $($permutation.variation_index) fragment GLSL")
}
$expectedSssBindings = @{
    'BaseColorMap' = @{ Sampler = 'fp_t_tcb_8'; Components = 'xyz' }
    'NormalMap' = @{ Sampler = 'fp_t_tcb_C'; Components = 'xy' }
    'RoughnessMap' = @{ Sampler = 'fp_t_tcb_10'; Components = 'x' }
    'AOMap' = @{ Sampler = 'fp_t_tcb_14'; Components = 'x' }
    'SSSMaskMap' = @{ Sampler = 'fp_t_tcb_1A'; Components = 'x' }
}
foreach ($role in $expectedSssBindings.Keys) {
    $binding = @($sssBindings[0].mapping |
        Where-Object material_role -eq $role)
    Assert-Condition ($binding.Count -eq 1 -and
        [string]$binding[0].anonymous_sampler -eq
            [string]$expectedSssBindings[$role].Sampler -and
        @($binding[0].sample_components).Count -eq 1 -and
        [string]$binding[0].sample_components[0] -eq
            [string]$expectedSssBindings[$role].Components) (
        "SSS differential binding changed for $role.")
}
$eyeBindings = @($bindingRows | Where-Object family -eq 'EyeClearCoat')
$eyeBaseColor1 = @($eyeBindings[0].mapping |
    Where-Object material_role -eq 'BaseColorMap1')
$eyeNormal1 = @($eyeBindings[0].mapping |
    Where-Object material_role -eq 'NormalMap1')
Assert-Condition ($eyeBindings.Count -eq 1 -and
    [string]$eyeBindings[0].status -eq 'partial_material_texture_bindings' -and
    $eyeBaseColor1.Count -eq 1 -and
    [string]$eyeBaseColor1[0].anonymous_sampler -eq 'fp_t_tcb_1A' -and
    $eyeNormal1.Count -eq 1 -and
    [string]$eyeNormal1[0].anonymous_sampler -eq 'fp_t_tcb_1E' -and
    [string]$eyeNormal1[0].material_constant -eq
        'NormalHeight1=fp_c7.data[4].w' -and
    @($eyeBindings[0].highlight_texture_binding_delta).Count -eq 0) (
    'EyeClearCoat differential proof changed.')
Assert-Condition (@($eyeBindings[0].permutations).Count -eq 4 -and
    @($eyeBindings[0].exact_variant_system_resources).Count -eq 1 -and
    [string]$eyeBindings[0].exact_variant_system_resources[0].anonymous_sampler -eq
        'fp_t_tcb_3E' -and
    [string]$eyeBindings[0].exact_variant_system_resources[0].classification -eq
        'projected_scene_scalar_resource') (
    'EyeClearCoat differential must retain four permutations and its projected resource boundary.')
foreach ($permutation in @($eyeBindings[0].permutations)) {
    Assert-Sha256 ([string]$permutation.fragment_glsl_sha256) (
        "Eye variation $($permutation.variation_index) fragment GLSL")
}

$constantRows = @($report.constant_buffer_mappings)
Assert-Condition ($constantRows.Count -eq 2) (
    'Static report must contain SSS and EyeClearCoat constant-buffer mappings.')
$sssConstants = @($constantRows | Where-Object family -eq 'SSS')
$eyeConstants = @($constantRows | Where-Object family -eq 'EyeClearCoat')
$expectedSssConstants = @{
    'UVScaleOffset' = 'fp_c8.data[1].xyzw'
    'NormalHeight' = 'fp_c7.data[4].z'
    'SSSMaskScale' = 'fp_c7.data[17].z'
    'SSSMaskOffset' = 'fp_c7.data[41].x'
    'SubsurfaceColor' = 'fp_c8.data[41].xyz'
}
$expectedEyeConstants = @{
    'UVRotation' = 'fp_c7.data[16].x'
    'UVScaleOffset' = 'fp_c8.data[1].xyzw'
    'NormalHeight1' = 'fp_c7.data[4].w'
    'MetallicClearCoat' = 'fp_c7.data[4].x'
    'RoughnessClearCoat' = 'fp_c7.data[7].w'
    'BaseColorClearCoat' = 'fp_c8.data[18].xyzw'
    'RoughnessHighlight' = 'fp_c7.data[57].w'
    'MetallicHighlight' = 'fp_c7.data[58].x'
    'EmissionIntensityLayer5' = 'fp_c7.data[9].y'
    'EmissionColorLayer5' = 'fp_c8.data[24].xyz'
}
foreach ($pair in @(
        @{ Row = $sssConstants; Expected = $expectedSssConstants; Label = 'SSS' },
        @{ Row = $eyeConstants; Expected = $expectedEyeConstants; Label = 'EyeClearCoat' })) {
    Assert-Condition (@($pair.Row).Count -eq 1) (
        "$($pair.Label) constant-buffer mapping is missing.")
    foreach ($name in $pair.Expected.Keys) {
        $mapping = @($pair.Row[0].mappings |
            Where-Object material_parameter -eq $name)
        Assert-Condition ($mapping.Count -eq 1 -and
            [string]$mapping[0].anonymous_field -eq
                [string]$pair.Expected[$name]) (
            "$($pair.Label) constant-buffer mapping changed for $name.")
    }
}
Assert-Condition ([int]$eyeConstants[0].highlight_differential.disabled_variation -eq 0 -and
    [int]$eyeConstants[0].highlight_differential.enabled_variation -eq 20 -and
    @($eyeConstants[0].highlight_differential.added_material_scalar_fields).Count -eq 3 -and
    [string]$eyeConstants[0].highlight_differential.added_scene_field -eq
        'fp_c8.data[96].xyzw') (
    'EyeClearCoat highlight constant differential changed.')

$textureRows = @($report.decoded_textures)
Assert-Condition ($textureRows.Count -eq 11) (
    'Static report must measure all 11 distinct Eevee decoded texture roles.')
$roles = @($textureRows.role | Sort-Object -Unique)
foreach ($requiredRole in @(
        'BaseColorMap', 'NormalMap', 'NormalMap1', 'RoughnessMap',
        'AOMap', 'SSSMaskMap', 'LayerMaskMap')) {
    Assert-Condition ($roles -contains $requiredRole) (
        "Static report is missing texture role $requiredRole.")
}
foreach ($texture in $textureRows) {
    $texturePath = Resolve-InputPath (
        Join-Path $GameRoot 'assets\models') ([string]$texture.decoded_path)
    Assert-Condition (Test-Path -LiteralPath $texturePath -PathType Leaf) (
        "Decoded texture is missing: $($texture.decoded_path)")
    $hash = (Get-FileHash -LiteralPath $texturePath -Algorithm SHA256).
        Hash.ToLowerInvariant()
    Assert-Condition ($hash -eq ([string]$texture.decoded_sha256).
        ToLowerInvariant()) (
        "Decoded texture hash mismatch: $($texture.decoded_path)")
}

$sssMask = @($textureRows | Where-Object role -eq 'SSSMaskMap')
Assert-Condition ($sssMask.Count -eq 1 -and
    [int]$sssMask[0].width -eq 16 -and
    [int]$sssMask[0].height -eq 16 -and
    [int]$sssMask[0].channels.red.unique_values -eq 1 -and
    [double]$sssMask[0].channels.red.mean -eq 255.0) (
    'Eevee SSS mask is no longer the measured constant-white 16x16 source map.')
$roughnessRows = @($textureRows | Where-Object role -eq 'RoughnessMap')
Assert-Condition ($roughnessRows.Count -eq 2 -and
    @($roughnessRows | Where-Object {
        [int]$_.width -ne 1024 -or [int]$_.height -ne 1024 -or
        [int]$_.channels.red.unique_values -lt 100
    }).Count -eq 0) (
    'Eevee roughness atlas measurements no longer match the retained source.')

$findingIds = @($report.findings.id)
foreach ($requiredFinding in @(
        'exact_program_selection', 'roughness_atlas_character',
        'sss_static_binding_contract', 'eye_static_binding_contract',
        'material_constant_buffer_contract', 'current_phlosion_gap')) {
    Assert-Condition ($findingIds -contains $requiredFinding) (
        "Static report is missing finding $requiredFinding.")
}

if (-not [string]::IsNullOrWhiteSpace($PromotedReportPath)) {
    $promotedPath = Resolve-InputPath $GameRoot $PromotedReportPath
    Assert-Condition (Test-Path -LiteralPath $promotedPath -PathType Leaf) (
        "Promoted static evidence is missing: $promotedPath")
    $promoted = Get-Content -LiteralPath $promotedPath -Raw |
        ConvertFrom-Json
    Assert-Condition ([string]$promoted.schema -eq
        'pokemon-autochess-static-character-material-evidence-v1') (
        'Unsupported promoted Eevee evidence schema.')
    Assert-Condition ([string]$promoted.subject.source_model_sha256 -eq
        [string]$report.subject.source_model_sha256 -and
        [string]$promoted.subject.payload_sha256 -eq
            [string]$report.subject.payload_sha256) (
        'Promoted Eevee subject identities do not match the generated report.')
    foreach ($expectedFamily in $expectedPrograms.Keys) {
        $generatedProgram = @($report.shader_evidence |
            Where-Object family -eq $expectedFamily)[0]
        $promotedProgram = @($promoted.resolved_programs |
            Where-Object family -eq $expectedFamily)[0]
        $generatedGlsl = @($generatedProgram.program_files |
            Where-Object kind -eq 'fsh.maxwell.glsl')[0]
        Assert-Condition ([int]$promotedProgram.variation_index -eq
            [int]$generatedProgram.resolved_variation -and
            [string]$promotedProgram.archive_sha256 -eq
                [string]$generatedProgram.archive.sha256 -and
            [string]$promotedProgram.metadata_sha256 -eq
                [string]$generatedProgram.metadata.sha256 -and
            [string]$promotedProgram.fragment_glsl_sha256 -eq
                [string]$generatedGlsl.sha256 -and
            [string]$promotedProgram.reflection_status -eq
                [string]$generatedProgram.archive.reflection.status -and
            [string]$promotedProgram.reflection_pointer_hex -eq
                [string]$generatedProgram.archive.reflection.
                    reflection_pointer_hex) (
            "Promoted $expectedFamily evidence differs from the generated report.")
    }
    $promotedSss = @($promoted.compiled_permutation_evidence |
        Where-Object family -eq 'SSS')
    Assert-Condition ($promotedSss.Count -eq 1 -and
        [string]$promotedSss[0].status -eq
            'exact_material_texture_bindings' -and
        [string]$promotedSss[0].mapping.RoughnessMap -eq
            'fp_t_tcb_10.x') (
        'Promoted SSS binding differential is missing or changed.')
    $promotedEye = @($promoted.compiled_permutation_evidence |
        Where-Object family -eq 'EyeClearCoat')
    Assert-Condition ($promotedEye.Count -eq 1 -and
        [string]$promotedEye[0].status -eq
            'partial_material_texture_bindings' -and
        [string]$promotedEye[0].mapping.NormalMap1 -eq
            'fp_t_tcb_1E.xy' -and
        [string]$promotedEye[0].system_resources.projected_scene_scalar -eq
            'fp_t_tcb_3E.x') (
        'Promoted EyeClearCoat binding boundary is missing or changed.')
    Assert-Condition (@($promoted.constant_buffer_mappings).Count -eq 2) (
        'Promoted constant-buffer mappings are missing.')
    $promotedEyeConstants = @($promoted.constant_buffer_mappings |
        Where-Object family -eq 'EyeClearCoat')
    foreach ($name in $expectedEyeConstants.Keys) {
        Assert-Condition ([string]$promotedEyeConstants[0].mapping.$name -eq
            [string]$expectedEyeConstants[$name]) (
            "Promoted EyeClearCoat constant changed for $name.")
    }
    Assert-Condition ([int]$promotedEyeConstants[0].highlight_differential.disabled_variation -eq 0 -and
        [int]$promotedEyeConstants[0].highlight_differential.enabled_variation -eq 20 -and
        [string]$promotedEyeConstants[0].highlight_differential.added_scene_field -eq
            'fp_c8.data[96].xyzw') (
        'Promoted EyeClearCoat highlight differential changed.')
    foreach ($family in @('SSS', 'EyeClearCoat')) {
        $generatedDifferential = @($bindingRows |
            Where-Object family -eq $family)[0]
        $promotedDifferential = @($promoted.compiled_permutation_evidence |
            Where-Object family -eq $family)[0]
        Assert-Condition (@($generatedDifferential.permutations).Count -eq
            @($promotedDifferential.permutations).Count) (
            "Promoted $family permutation count differs from generated evidence.")
        foreach ($generatedPermutation in @($generatedDifferential.permutations)) {
            $generatedVariation = [int]$generatedPermutation.variation_index
            $promotedPermutation = @($promotedDifferential.permutations |
                Where-Object { [int]$_.variation_index -eq $generatedVariation })
            Assert-Condition ($promotedPermutation.Count -eq 1 -and
                [string]$promotedPermutation[0].shader_key_hex -eq
                    [string]$generatedPermutation.shader_key_hex -and
                [string]$promotedPermutation[0].global_key_hex -eq
                    [string]$generatedPermutation.global_key_hex -and
                [string]$promotedPermutation[0].fragment_glsl_sha256 -eq
                    [string]$generatedPermutation.fragment_glsl_sha256) (
                "Promoted $family variation $($generatedPermutation.variation_index) differs from generated evidence.")
        }
    }
    foreach ($measurement in @($promoted.key_texture_measurements)) {
        $generatedTexture = @($textureRows | Where-Object {
            [string]$_.decoded_path -eq [string]$measurement.decoded_path
        })
        Assert-Condition ($generatedTexture.Count -eq 1 -and
            [string]$generatedTexture[0].decoded_sha256 -eq
                [string]$measurement.decoded_sha256 -and
            [int]$generatedTexture[0].width -eq [int]$measurement.width -and
            [int]$generatedTexture[0].height -eq [int]$measurement.height -and
            [double]$generatedTexture[0].channels.red.mean -eq
                [double]$measurement.red_mean) (
            "Promoted texture measurement differs: $($measurement.decoded_path)")
    }
}

$summary = [pscustomobject][ordered]@{
    subject = '0133 Eevee'
    source = [string]$report.subject.source_profile
    textures = $textureRows.Count
    shader_programs = @($report.shader_evidence).Count
    exact_sss_texture_bindings = @($sssBindings[0].mapping).Count
    sss_variation = 56
    eye_variation = 20
    emulator_used = $false
}
Write-Host (
    'SV Eevee static material report valid: textures={0}, shaders={1}, SSS=v{2}, EyeClearCoat=v{3}, emulator=false.' -f
        $summary.textures,
        $summary.shader_programs,
        $summary.sss_variation,
        $summary.eye_variation)
$summary
