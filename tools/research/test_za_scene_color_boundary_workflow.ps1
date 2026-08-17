param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$analyzer = Join-Path $PSScriptRoot 'analyze_za_scene_color_boundary.py'
$promoted = Join-Path $gameRoot (
    'docs\kanto\evidence\za_scene_color_boundary.json')

Assert-Condition (Test-Path -LiteralPath $analyzer -PathType Leaf) (
    'Z-A scene/color boundary analyzer is missing.')
$source = Get-Content -LiteralPath $analyzer -Raw
foreach ($token in @(
        'cross_family_compiled_operation_identity_plus_symbolic_',
        'camera_relative_vector', 'final_scene_fade',
        'dominant_direct_light_contract', 'diffuse_irradiance_contract',
        'ik_indexed_light_contract',
        'ik_local_reflection_direction_contract',
        'shadow_sampling_contract', 'ik_scene_light_contract',
        'max_abs_normalize(vec3(n.x, n.y, -n.z))',
        'reflect(-view, mapped_normal)',
        'fp_c4[1 + light_index].rgb / pi',
        'fp_c4[27 + light_index].rgb',
        'max(direct_diffuse_rgb)', '0.318309873', 'fp_c7[97].w^2',
        'ReceiveShadow', 'PostEffectsToneMap',
        'runtime_execution": False', 'emulator_used": False')) {
    Assert-Condition ($source.Contains($token)) (
        "Z-A scene/color analyzer lost contract token: $token")
}

Assert-Condition (Test-Path -LiteralPath $promoted -PathType Leaf) (
    'Promoted Z-A scene/color boundary evidence is missing.')
$report = Get-Content -LiteralPath $promoted -Raw | ConvertFrom-Json
Assert-Condition ([string]$report.schema -eq
    'pokemon-autochess-za-scene-color-boundary-evidence-v3') (
    'Promoted Z-A scene/color evidence has the wrong schema.')
Assert-Condition (-not [bool]$report.method.runtime_execution -and
    -not [bool]$report.method.emulator_used) (
    'Promoted Z-A scene/color evidence must remain emulator-free.')
Assert-Condition (
    [int]$report.summary.material_fragment_programs -eq 7 -and
    [int]$report.summary.material_shader_families -eq 4 -and
    [int]$report.summary.programs_with_exact_final_scene_fade -eq 7 -and
    [int]$report.summary.programs_with_camera_position_classification -eq 7 -and
    [int]$report.summary.programs_with_dominant_light_direction -eq 7 -and
    [int]$report.summary.programs_with_diffuse_irradiance_direction -eq 7 -and
    [int]$report.summary.receive_shadow_identical_fragment_edges -eq 3 -and
    [int]$report.summary.receive_shadow_enabled_materials -eq 226 -and
    [int]$report.summary.programs_with_exact_scene_shadow_sampling -eq 7 -and
    [int]$report.summary.ik_programs_with_exact_scene_light_inputs -eq 4 -and
    [int]$report.summary.ik_programs_with_indexed_light_records -eq 4 -and
    [int]$report.summary.ik_programs_with_exact_local_reflection_direction -eq 4 -and
    [int]$report.summary.tonemap_programs -eq 1 -and
    [int]$report.summary.runtime_changes_authorized_by_this_report -eq 3) (
    'Promoted Z-A scene/color coverage changed.')
Assert-Condition (
    [string]$report.shared_scene_fields.camera_world_position -eq
        'fp_c5[19].xyz' -and
    [string]$report.shared_scene_fields.dominant_directional_light_vector -eq
        'fp_c4[0].xyz' -and
    [string]$report.shared_scene_fields.final_scene_color -eq
        'fp_c10[12].rgb' -and
    [string]$report.shared_scene_fields.final_scene_fade -eq
        'fp_c10[12].w') (
    'Promoted Z-A shared scene-field boundary changed.')
foreach ($program in @($report.material_programs)) {
    Assert-Condition (
        [string]$program.final_scene_fade.proof -eq
            'compiled_final_output_operation_identity' -and
        [string]$program.camera_relative_vector.semantic_classification -eq
            'camera_world_position' -and
        [string]$program.dominant_direct_light.fragment_to_light_direction -eq
            '-fp_c4[0].xyz' -and
        [string]$program.diffuse_irradiance.direction -eq
            'max_abs_normalize(vec3(n.x, n.y, -n.z))' -and
        [int]$program.diffuse_irradiance.lod -eq 0 -and
        [int]$program.scene_shadow.filter.cascade_taps -eq 16 -and
        [string]$program.scene_shadow.filter.tap_weight -eq '1/16') (
        "Z-A material scene-boundary proof changed: $($program.shader_family)")
}
foreach ($program in @($report.material_programs | Where-Object {
        $_.shader_family -eq 'IkCharacter' })) {
    Assert-Condition (
        [string]$program.ik_indexed_light.direct_record.field -eq
            'fp_c4[1 + light_index].rgb' -and
        [string]$program.ik_indexed_light.direct_record.presence_gate -eq
            'sum(rgb) > 0' -and
        [string]$program.ik_indexed_light.diffuse_environment_record.field -eq
            'fp_c4[27 + light_index].rgb' -and
        [int]$program.ik_indexed_light.inverse_pi_terms.direct_rgb -eq 3 -and
        [int]$program.ik_indexed_light.inverse_pi_terms.diffuse_environment_rgb -eq 3 -and
        [string]$program.ik_local_reflection_direction.equivalent_cubemap_direction -eq
            'reflect(-view, mapped_normal)' -and
        -not [bool]$program.ik_local_reflection_direction.coordinate_sign_flip) (
        "Z-A IkCharacter environment proof changed: $($program.variation_index)")
}
Assert-Condition (
    [string]$report.receive_shadow.fragment_classification -eq
        'identical_compiled_output_slice' -and
    [int]$report.receive_shadow_inventory.enabled_materials -eq 226 -and
    [string]$report.scene_light_composition.ik_direct_light_visibility -eq
        'clamp(combined_scene_shadow_visibility + fp_c7[97].w^2, 0, 1)' -and
    [string]$report.scene_light_composition.ik_middle_dark_input -eq
        'max(direct_diffuse_rgb)' -and
    [string]$report.scene_light_composition.direct_diffuse_normalization -eq
        'three inverse-pi channel terms' -and
    [string]$report.scene_light_composition.indexed_direct_radiance -eq
        'fp_c4[1 + light_index].rgb / pi' -and
    [string]$report.scene_light_composition.ik_local_reflection_direction -eq
        'max_abs_normalize(reflect(-view, mapped_normal))' -and
    [string]$report.post_effect_tonemap.exact_equations.exposure_multiplier -eq
        'exp(fp_c3[114].w)' -and
    [string]$report.post_effect_tonemap.exact_equations.output_transfer -eq
        'piecewise_srgb_oetf' -and
    @($report.post_effect_tonemap.unavailable_runtime_values).Count -eq 5) (
    'Promoted Z-A receive-shadow or tone-map boundary changed.')

Write-Host 'Z-A scene/color boundary workflow contract passed.'
