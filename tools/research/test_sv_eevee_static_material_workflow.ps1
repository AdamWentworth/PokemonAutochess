param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$validator = Join-Path $PSScriptRoot 'validate_sv_eevee_static_material.ps1'
$analyzer = Join-Path $PSScriptRoot 'analyze_sv_eevee_static_material.py'
$extractor = Join-Path $PSScriptRoot (
    'extract_sv_eevee_shader_permutations.ps1')
$promoted = Join-Path $gameRoot (
    'docs\kanto\evidence\sv_eevee_static_material_report.json')

Assert-Condition (Test-Path -LiteralPath $analyzer -PathType Leaf) (
    'SV Eevee static analyzer is missing.')
Assert-Condition (Test-Path -LiteralPath $extractor -PathType Leaf) (
    'SV Eevee offline shader-permutation extractor is missing.')
$extractorSource = Get-Content -LiteralPath $extractor -Raw
foreach ($variation in @('0, 8, 40, 48, 56', '0, 20, 44, 52')) {
    Assert-Condition ($extractorSource.Contains($variation)) (
        "Offline extractor lost permutation set: $variation")
}
foreach ($stage in @('fsh.maxwell.glsl', 'vsh.maxwell.glsl')) {
    Assert-Condition ($extractorSource.Contains($stage)) (
        "Offline extractor no longer decompiles $stage files.")
}
$source = Get-Content -LiteralPath $analyzer -Raw
foreach ($contractToken in @(
        'runtime_execution": False', 'emulator_used": False',
        'bool1', 'param_buffer', 'RoughnessMap',
        'anisotropic or fibre-direction shader lobe',
        'compiled_option_permutation_set_difference',
        'absent_or_stripped', 'SSSMaskScale', 'NormalHeight1',
        'MetallicClearCoat', 'RoughnessHighlight',
        'EmissionColorLayer5',
        'projected_scene_scalar_resource',
        'specular_environment_cube', 'diffuse_irradiance_cube',
        'highlight_point_light_position', 'highlight_point_light_enable')) {
    Assert-Condition ($source.Contains($contractToken)) (
        "Static analyzer is missing contract token: $contractToken")
}

$promotedJson = Get-Content -LiteralPath $promoted -Raw | ConvertFrom-Json
Assert-Condition ([string]$promotedJson.schema -eq
    'pokemon-autochess-static-character-material-evidence-v1') (
    'Promoted SV Eevee evidence has the wrong schema.')
Assert-Condition (-not [bool]$promotedJson.method.runtime_execution -and
    -not [bool]$promotedJson.method.emulator_used) (
    'Promoted static evidence must remain emulator-free.')
$sss = @($promotedJson.resolved_programs | Where-Object family -eq 'SSS')
$eye = @($promotedJson.resolved_programs |
    Where-Object family -eq 'EyeClearCoat')
Assert-Condition ($sss.Count -eq 1 -and
    [int]$sss[0].variation_index -eq 56 -and
    [string]$sss[0].shader_key_hex -eq '0x41F' -and
    [string]$sss[0].global_key_hex -eq '0x1') (
    'Promoted SSS program identity changed.')
Assert-Condition ($eye.Count -eq 1 -and
    [int]$eye[0].variation_index -eq 20 -and
    [string]$eye[0].shader_key_hex -eq '0x24' -and
    [string]$eye[0].global_key_hex -eq '0x0') (
    'Promoted EyeClearCoat program identity changed.')
Assert-Condition (@($promotedJson.conclusions | Where-Object {
    $_ -like '*not a two-component fibre-direction texture*'
}).Count -eq 1) (
    'Promoted evidence lost the scalar-roughness/fibre boundary.')
$promotedSss = @($promotedJson.compiled_permutation_evidence |
    Where-Object family -eq 'SSS')
Assert-Condition ($promotedSss.Count -eq 1 -and
    [string]$promotedSss[0].mapping.BaseColorMap -eq 'fp_t_tcb_8.xyz' -and
    [string]$promotedSss[0].mapping.NormalMap -eq 'fp_t_tcb_C.xy' -and
    [string]$promotedSss[0].mapping.RoughnessMap -eq 'fp_t_tcb_10.x' -and
    [string]$promotedSss[0].mapping.AOMap -eq 'fp_t_tcb_14.x' -and
    [string]$promotedSss[0].mapping.SSSMaskMap -eq 'fp_t_tcb_1A.x') (
    'Promoted evidence lost exact SSS material bindings.')
$promotedEye = @($promotedJson.compiled_permutation_evidence |
    Where-Object family -eq 'EyeClearCoat')
Assert-Condition ($promotedEye.Count -eq 1 -and
    [string]$promotedEye[0].mapping.NormalMap1 -eq 'fp_t_tcb_1E.xy' -and
    [string]$promotedEye[0].system_resources.projected_scene_scalar -eq
        'fp_t_tcb_3E.x') (
    'Promoted evidence overstates or loses the EyeClearCoat binding boundary.')
Assert-Condition (@($promotedJson.constant_buffer_mappings).Count -eq 2) (
    'Promoted evidence lost material constant-buffer mappings.')
$promotedEyeConstants = @($promotedJson.constant_buffer_mappings |
    Where-Object family -eq 'EyeClearCoat')
Assert-Condition ($promotedEyeConstants.Count -eq 1 -and
    [string]$promotedEyeConstants[0].mapping.MetallicClearCoat -eq
        'fp_c7.data[4].x' -and
    [string]$promotedEyeConstants[0].mapping.RoughnessClearCoat -eq
        'fp_c7.data[7].w' -and
    [string]$promotedEyeConstants[0].mapping.BaseColorClearCoat -eq
        'fp_c8.data[18].xyzw' -and
    [string]$promotedEyeConstants[0].mapping.RoughnessHighlight -eq
        'fp_c7.data[57].w' -and
    [string]$promotedEyeConstants[0].mapping.MetallicHighlight -eq
        'fp_c7.data[58].x' -and
    [string]$promotedEyeConstants[0].mapping.EmissionIntensityLayer5 -eq
        'fp_c7.data[9].y' -and
    [string]$promotedEyeConstants[0].mapping.EmissionColorLayer5 -eq
        'fp_c8.data[24].xyz') (
    'Promoted evidence lost the exact EyeClearCoat material constants.')
$promotedSssConstants = @($promotedJson.constant_buffer_mappings |
    Where-Object family -eq 'SSS')
$sssSpecularCube = @($promotedSssConstants[0].scene_input_mappings |
    Where-Object anonymous_field -eq 'fp_t_tcb_36')
$sssDiffuseCube = @($promotedSssConstants[0].scene_input_mappings |
    Where-Object anonymous_field -eq 'fp_t_tcb_34')
$eyePointLightPosition = @($promotedEyeConstants[0].scene_input_mappings |
    Where-Object anonymous_field -eq 'fp_c8.data[96].xyz')
$eyePointLightEnable = @($promotedEyeConstants[0].scene_input_mappings |
    Where-Object anonymous_field -eq 'fp_c8.data[96].w')
Assert-Condition ($sssSpecularCube.Count -eq 1 -and
    [string]$sssSpecularCube[0].classification -eq
        'specular_environment_cube' -and
    $sssDiffuseCube.Count -eq 1 -and
    [string]$sssDiffuseCube[0].classification -eq
        'diffuse_irradiance_cube' -and
    $eyePointLightPosition.Count -eq 1 -and
    [string]$eyePointLightPosition[0].classification -eq
        'highlight_point_light_position' -and
    $eyePointLightEnable.Count -eq 1 -and
    [string]$eyePointLightEnable[0].classification -eq
        'highlight_point_light_enable') (
    'Promoted evidence lost the SSS environment or eye point-light mappings.')

$fixtureReport = Join-Path ([IO.Path]::GetTempPath()) (
    'pokemonautochess-sv-eevee-static-' + [Guid]::NewGuid().ToString('N') +
    '.json')
try {
    $fixture = [ordered]@{
        schema = 'pokemon-autochess-static-character-material-report-v1'
        subject = [ordered]@{
            species_id = 133
            species_name = 'Eevee'
            source_profile = 'pokemon-scarlet-v3.0.1'
            canonical_manifest = 'assets/models/0133_Eevee_SV.phmodel'
            source_model_sha256 = [string]$promotedJson.subject.source_model_sha256
            payload_sha256 = [string]$promotedJson.subject.payload_sha256
        }
        method = [ordered]@{
            runtime_execution = $false
            emulator_used = $false
        }
        shader_evidence = @(
            foreach ($program in @($promotedJson.resolved_programs)) {
                [ordered]@{
                    family = [string]$program.family
                    resolved_variation = [int]$program.variation_index
                    archive = [ordered]@{ sha256 = [string]$program.archive_sha256 }
                    metadata = [ordered]@{ sha256 = [string]$program.metadata_sha256 }
                    material_keys = @([ordered]@{
                        shader_key_hex = [string]$program.shader_key_hex
                        global_key_hex = [string]$program.global_key_hex
                    })
                    program_files = @([ordered]@{
                        kind = 'fsh.maxwell.glsl'
                        sha256 = [string]$program.fragment_glsl_sha256
                    })
                }
            }
        )
        decoded_textures = @()
        findings = @()
    }
    # This deliberately incomplete fixture must be rejected after program and
    # subject validation, proving the validator requires the full static
    # evidence contract rather than accepting program identities alone.
    [IO.File]::WriteAllText(
        $fixtureReport,
        ($fixture | ConvertTo-Json -Depth 12),
        (New-Object Text.UTF8Encoding($false)))
    $rejected = $false
    try {
        & $validator -GameRoot $gameRoot -ReportPath $fixtureReport 2>$null |
            Out-Null
    } catch {
        $rejected = $true
    }
    Assert-Condition $rejected (
        'Static validator accepted an incomplete evidence report.')
} finally {
    Remove-Item -LiteralPath $fixtureReport -Force -ErrorAction SilentlyContinue
}

Write-Host '[SvEeveeStaticMaterialWorkflowTest] PASS'
