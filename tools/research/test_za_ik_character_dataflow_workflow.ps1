param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$analyzer = Join-Path $PSScriptRoot 'analyze_za_ik_character_dataflow.py'
$promoted = Join-Path $gameRoot (
    'docs\kanto\evidence\za_ik_character_dataflow_report.json')

Assert-Condition (Test-Path -LiteralPath $analyzer -PathType Leaf) (
    'Z-A IkCharacter dataflow analyzer is missing.')
$source = Get-Content -LiteralPath $analyzer -Raw
foreach ($token in @(
        'conservative_compiled_ssa_output_slice',
        'backward_closure', 'forward_closure',
        'body_constant_buffer_data_flow', 'bnsh_reflection_report',
        'final_scene_fade_boundary',
        'cooked_body_emission_verification',
        'EnableHairSpecular', 'fp_t_tcb_1A',
        'runtime_execution": False', 'emulator_used": False')) {
    Assert-Condition ($source.Contains($token)) (
        "Z-A IkCharacter dataflow analyzer lost contract token: $token")
}

Assert-Condition (Test-Path -LiteralPath $promoted -PathType Leaf) (
    'Promoted Z-A IkCharacter dataflow report is missing.')
$report = Get-Content -LiteralPath $promoted -Raw | ConvertFrom-Json
Assert-Condition ([string]$report.schema -eq
    'pokemon-autochess-za-ik-character-dataflow-evidence-v2') (
    'Promoted Z-A IkCharacter dataflow evidence has the wrong schema.')
Assert-Condition (-not [bool]$report.method.runtime_execution -and
    -not [bool]$report.method.emulator_used) (
    'Promoted Z-A IkCharacter dataflow evidence must remain emulator-free.')
Assert-Condition ([int]$report.summary.selected_programs -eq 4 -and
    [int]$report.summary.selected_materials -eq 222 -and
    [int]$report.summary.ordinary_body_materials -eq 140 -and
    [int]$report.summary.output_reachable_body_resources -eq 13 -and
    [int]$report.summary.hair_specular_enabled_materials -eq 0 -and
    [int]$report.summary.hair_specular_single_option_differentials -eq 3 -and
    [int]$report.summary.mapped_body_material_fields -eq 19 -and
    [int]$report.summary.selected_programs_with_stripped_reflection -eq 4 -and
    [int]$report.summary.mapped_eye_material_fields -eq 7 -and
    [int]$report.summary.cooked_phmat_files_verified -eq 52 -and
    [int]$report.summary.cooked_mode32_submesh_records_verified -eq 184 -and
    [int]$report.summary.cooked_body_emission_records_verified -eq 2 -and
    [int]$report.summary.cooked_neutral_hair_auxiliary_records_verified -eq 184 -and
    [int]$report.summary.selected_programs_with_exact_final_scene_fade -eq 4 -and
    [string]$report.summary.ordinary_displaced_body_fragment_identity -eq
        'identical' -and
    [int]$report.summary.runtime_changes_authorized_by_this_report -eq 3) (
    'Promoted Z-A IkCharacter dataflow coverage changed.')
Assert-Condition (
    [string]$report.shared_material_buffer_mappings.NormalHeight -eq
        'fp_c7[4].z' -and
    [string]$report.shared_material_buffer_mappings.LayerMaskScale4 -eq
        'fp_c7[11].x' -and
    [string]$report.shared_material_buffer_mappings.EmissionIntensity -eq
        'fp_c7[8].y' -and
    [string]$report.shared_material_buffer_mappings.ReflectionsBlur -eq
        'fp_c7[101].w' -and
    [string]$report.shared_material_buffer_mappings.RimLightIntensity -eq
        'fp_c7[101].x' -and
    [string]$report.shared_material_buffer_mappings.BaseColorLayer4 -eq
        'fp_c8[13].xyzw') (
    'Promoted shared Z-A material-buffer mapping changed.')
Assert-Condition (
    [string]$report.body_constant_buffer_data_flow.layer_mask_scales.proof -eq
        'compiled_operation_identity' -and
    [string]$report.body_constant_buffer_data_flow.rim_mask.sampled_channel -eq
        'RimLightMaskMap.r' -and
    [int]$report.ordinary_body_parameter_census.RimLightIntensity.'0.8' -eq
        138 -and
    [int]$report.ordinary_body_parameter_census.EmissionIntensity.'0' -eq
        140 -and
    [int]$report.ordinary_body_parameter_census.EmissionIntensityLayer1.'0' -eq
        140 -and
    [int]$report.ordinary_body_parameter_census.EmissionIntensityLayer2.'0' -eq
        140 -and
    [int]$report.ordinary_body_parameter_census.EmissionIntensityLayer3.'0' -eq
        138 -and
    [int]$report.ordinary_body_parameter_census.EmissionIntensityLayer3.'0.5' -eq
        2 -and
    [int]$report.ordinary_body_parameter_census.EmissionIntensityLayer4.'0' -eq
        140) (
    'Promoted ordinary-body operation/census evidence changed.')
Assert-Condition (
    [int]$report.cooked_body_emission_verification.neutral_mode32_emission_lanes_verified -eq
        182 -and
    [int]$report.cooked_body_emission_verification.neutral_hair_auxiliary_records_verified -eq
        184 -and
    @($report.cooked_body_emission_verification.emission_records).Count -eq 2 -and
    @($report.cooked_body_emission_verification.emission_records |
        Where-Object {
            [int]$_.packed_blue_channel.maximum_blue -eq 188 -and
            [int]$_.packed_blue_channel.half_linear_srgb_byte_pixels -gt 0
        }).Count -eq 2) (
    'Promoted cooked Z-A body-emission transport evidence changed.')
Assert-Condition (
    [string]$report.eye_material_buffer_mappings.ParallaxHeight -eq
        'fp_c7[5].y' -and
    [string]$report.eye_material_buffer_mappings.ParallaxIOR -eq
        'fp_c7[5].z' -and
    [string]$report.eye_material_buffer_mappings.UVScaleOffset2 -eq
        'fp_c8[3].xyzw' -and
    [string]$report.eye_material_buffer_mappings.UVCenter1 -eq
        'fp_c8[140].xy') (
    'Promoted Z-A eye material-buffer mapping changed.')
Assert-Condition (
    [string]$report.hair_specular.selected_program_sampler -eq 'absent' -and
    [string]$report.hair_specular.optional_branch_sampler -eq
        'fp_t_tcb_1A' -and
    [int]$report.hair_specular.selected_material_choices.False -eq 222) (
    'Promoted Z-A HairSpecular boundary changed.')
$bodyRoles = @($report.body_resource_dependencies |
    Where-Object { [bool]$_.output_reachable } |
    Select-Object -ExpandProperty role)
foreach ($role in @(
        'BaseColorMap', 'NormalMap', 'OcclusionMap', 'SpecularMaskMap',
        'ShadowingColorMap', 'ShadowingColorMaskMap', 'LayerMaskMap',
        'RimLightMaskMap', 'LocalReflectionMap', 'DiffuseIrradianceCube')) {
    Assert-Condition ($bodyRoles -contains $role) (
        "Promoted Z-A body output slice lost role: $role")
}
foreach ($program in @($report.programs)) {
    Assert-Condition (
        [string]$program.reflection.status -eq 'absent_or_stripped' -and
        [string]$program.reflection.reflection_pointer_hex -eq '0x0' -and
        [string]$program.final_scene_fade.proof -eq
            'compiled_final_output_operation_identity') (
        "Selected Z-A program unexpectedly retained reflection: " +
        $program.variation_index)
}

Write-Host 'Z-A IkCharacter compiled-dataflow workflow contract passed.'
