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
        'eye_constant_buffer_data_flow',
        'final_scene_fade_boundary',
        'cooked_body_emission_verification',
        'phmat_mode_emissive_and_native_parameters',
        'back_rim_gate', 'direct_specular_boundary',
        'compiled_backward_dependency_closure',
        'EnableHairSpecular', 'fp_t_tcb_1A',
        'BaseColorDarkness', 'SpecularMaskMapValue',
        'CachedTextureRgba shadowingColorMask',
        'packIkCharacterEmissionColor',
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
Assert-Condition ([int]$report.summary.selected_programs -eq 5 -and
    [int]$report.summary.selected_materials -eq 1036 -and
    [int]$report.summary.ordinary_body_materials -eq 604 -and
    [int]$report.summary.output_reachable_body_resources -eq 13 -and
    [int]$report.summary.hair_specular_enabled_materials -eq 0 -and
    [int]$report.summary.hair_specular_single_option_differentials -eq 3 -and
    [int]$report.summary.mapped_body_material_fields -eq 64 -and
    [int]$report.summary.selected_programs_with_stripped_reflection -eq 5 -and
    [int]$report.summary.mapped_eye_material_fields -eq 10 -and
    [int]$report.summary.cooked_phmat_files_verified -eq 212 -and
    [int]$report.summary.cooked_mode32_submesh_records_verified -gt 0 -and
    [int]$report.summary.cooked_mode32_native_parameter_records_verified -eq
        [int]$report.summary.cooked_mode32_submesh_records_verified -and
    [int]$report.summary.cooked_body_emission_records_verified -eq 4 -and
    [int]$report.summary.cooked_neutral_hair_auxiliary_records_verified -eq
        [int]$report.summary.cooked_mode32_submesh_records_verified -and
    [int]$report.summary.selected_programs_with_exact_final_scene_fade -eq 5 -and
    [string]$report.summary.ordinary_displaced_body_fragment_identity -eq
        'identical' -and
    [int]$report.summary.eye_variations_with_exact_parallax_march -eq 2 -and
    [int]$report.summary.runtime_changes_authorized_by_this_report -eq 8) (
    'Promoted Z-A IkCharacter dataflow coverage changed.')
Assert-Condition (
    [int]$report.eye_parallax_data_flow.view_schedule.sample_count_range[0] -eq 4 -and
    [int]$report.eye_parallax_data_flow.view_schedule.sample_count_range[1] -eq 14 -and
    [string]$report.eye_parallax_data_flow.height_march.height_source -eq
        'ParallaxMap.r' -and
    [string]$report.eye_parallax_data_flow.height_march.hit_test -eq
        'sampled_height >= current_depth') (
    'Promoted Z-A IkCharacter exact eye-parallax proof changed.')
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
    [string]$report.shared_material_buffer_mappings.HalfLambertBias -eq
        'fp_c7[99].x' -and
    [string]$report.shared_material_buffer_mappings.RimLightOffset -eq
        'fp_c7[100].z' -and
    [string]$report.shared_material_buffer_mappings.RimLightContrast -eq
        'fp_c7[100].w' -and
    [string]$report.shared_material_buffer_mappings.MidAreaHueOffset -eq
        'fp_c7[102].w' -and
    [string]$report.shared_material_buffer_mappings.DarkAreaHueOffset -eq
        'fp_c7[103].z' -and
    [string]$report.shared_material_buffer_mappings.ShadowingContrast -eq
        'fp_c7[104].y' -and
    [string]$report.shared_material_buffer_mappings.BaseColorLayer4 -eq
        'fp_c8[13].xyzw') (
    'Promoted shared Z-A material-buffer mapping changed.')
Assert-Condition (
    [string]$report.body_constant_buffer_data_flow.layer_mask_scales.proof -eq
        'compiled_operation_identity' -and
    [string]$report.body_constant_buffer_data_flow.rim_shape.proof -eq
        'compiled_operation_identity' -and
    [string]$report.body_constant_buffer_data_flow.shadowing_bias_response.operation -eq
        'clamp(x + ShadowingBias * (x^2 - x), 0, 1)' -and
    [string]$report.body_constant_buffer_data_flow.color_process_layout.proof -eq
        'compiled_register_group_plus_backward_dependency_closure_plus_operation_identity' -and
    [string]$report.body_constant_buffer_data_flow.color_process_layout.hue_target_dependencies.middle.exclusive_authored_hue_dependency -eq
        'fp_c7[102].w' -and
    [string]$report.body_constant_buffer_data_flow.color_process_layout.hue_target_dependencies.dark.exclusive_authored_hue_dependency -eq
        'fp_c7[103].z' -and
    [string]$report.body_constant_buffer_data_flow.back_rim_gate.proof -eq
        'compiled_operation_identity' -and
    [string]$report.body_constant_buffer_data_flow.local_reflection.proof -eq
        'compiled_metallic_branch_plus_lod_plus_floor_identity' -and
    [string]$report.body_constant_buffer_data_flow.direct_specular_boundary.proof -eq
        'compiled_operation_and_branch_identity' -and
    [string]$report.body_constant_buffer_data_flow.rim_mask.sampled_channel -eq
        'RimLightMaskMap.r' -and
    [int]$report.ordinary_body_parameter_census.EmissionIntensityLayer3.'0.5' -eq
        2 -and
    [int]$report.ordinary_body_parameter_census.EmissionIntensityLayer3.'0' -eq
        602) (
    'Promoted ordinary-body operation/census evidence changed.')
Assert-Condition (
    [int]$report.cooked_body_emission_verification.neutral_mode32_emission_lanes_verified -eq
        ([int]$report.cooked_body_emission_verification.mode32_submesh_records_verified - 4) -and
    [int]$report.cooked_body_emission_verification.mode32_native_parameter_records_verified -eq
        [int]$report.cooked_body_emission_verification.mode32_submesh_records_verified -and
    [int]$report.cooked_body_emission_verification.neutral_hair_auxiliary_records_verified -eq
        [int]$report.cooked_body_emission_verification.mode32_submesh_records_verified -and
    @($report.cooked_body_emission_verification.emission_records).Count -eq 4 -and
    @($report.cooked_body_emission_verification.emission_records |
        Where-Object {
            [string]$_.stem -like '0120_Staryu_ZA*' -and
            [int]$_.packed_blue_channel.maximum_blue -eq 188 -and
            [int]$_.packed_blue_channel.half_linear_srgb_byte_pixels -gt 0
        }).Count -eq 2 -and
    @($report.cooked_body_emission_verification.emission_records |
        Where-Object {
            [string]$_.stem -like '0026_Raichu_ZA_MegaX*' -and
            [int]$_.packed_material_emission_color -eq 8023339
        }).Count -eq 2) (
    'Promoted cooked Z-A body-emission transport evidence changed.')
Assert-Condition (
    [string]$report.eye_material_buffer_mappings.ParallaxHeight -eq
        'fp_c7[5].y' -and
    [string]$report.eye_material_buffer_mappings.ParallaxIOR -eq
        'fp_c7[5].z' -and
    [string]$report.eye_material_buffer_mappings.EmissionIntensityLayer5 -eq
        'fp_c7[9].z' -and
    [string]$report.eye_material_buffer_mappings.BaseColorLayer6 -eq
        'fp_c8[15].xyzw' -and
    [string]$report.eye_material_buffer_mappings.EmissionColorLayer5 -eq
        'fp_c8[24].xyzw' -and
    [string]$report.eye_material_buffer_mappings.UVScaleOffset2 -eq
        'fp_c8[3].xyzw' -and
    [string]$report.eye_material_buffer_mappings.UVCenter1 -eq
        'fp_c8[140].xy') (
    'Promoted Z-A eye material-buffer mapping changed.')
Assert-Condition (
    [string]$report.eye_constant_buffer_data_flow.layer5_highlight.proof -eq
        'compiled_operation_identity_plus_output_reachability' -and
    [string]$report.eye_constant_buffer_data_flow.eyelid_shadow.proof -eq
        'compiled_operation_identity_plus_output_reachability' -and
    [string]$report.eye_constant_buffer_data_flow.shared_shadow_and_surface.proof -eq
        'compiled_operation_and_output_reachability') (
    'Promoted Z-A eye composite-order evidence changed.')
Assert-Condition (
    [string]$report.hair_specular.selected_program_sampler -eq 'absent' -and
    [string]$report.hair_specular.optional_branch_sampler -eq
        'fp_t_tcb_1A' -and
    [int]$report.hair_specular.selected_material_choices.False -eq 1036) (
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
