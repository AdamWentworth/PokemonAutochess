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
        'cross_family_compiled_operation_identity_plus_complete_',
        'camera_relative_vector', 'final_scene_fade',
        'ReceiveShadow', 'PostEffectsToneMap',
        'runtime_execution": False', 'emulator_used": False')) {
    Assert-Condition ($source.Contains($token)) (
        "Z-A scene/color analyzer lost contract token: $token")
}

Assert-Condition (Test-Path -LiteralPath $promoted -PathType Leaf) (
    'Promoted Z-A scene/color boundary evidence is missing.')
$report = Get-Content -LiteralPath $promoted -Raw | ConvertFrom-Json
Assert-Condition ([string]$report.schema -eq
    'pokemon-autochess-za-scene-color-boundary-evidence-v1') (
    'Promoted Z-A scene/color evidence has the wrong schema.')
Assert-Condition (-not [bool]$report.method.runtime_execution -and
    -not [bool]$report.method.emulator_used) (
    'Promoted Z-A scene/color evidence must remain emulator-free.')
Assert-Condition (
    [int]$report.summary.material_fragment_programs -eq 7 -and
    [int]$report.summary.material_shader_families -eq 4 -and
    [int]$report.summary.programs_with_exact_final_scene_fade -eq 7 -and
    [int]$report.summary.programs_with_camera_position_classification -eq 7 -and
    [int]$report.summary.receive_shadow_identical_fragment_edges -eq 3 -and
    [int]$report.summary.tonemap_programs -eq 1 -and
    [int]$report.summary.runtime_changes_authorized_by_this_report -eq 1) (
    'Promoted Z-A scene/color coverage changed.')
Assert-Condition (
    [string]$report.shared_scene_fields.camera_world_position -eq
        'fp_c5[19].xyz' -and
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
            'camera_world_position') (
        "Z-A material scene-boundary proof changed: $($program.shader_family)")
}
Assert-Condition (
    [string]$report.receive_shadow.fragment_classification -eq
        'identical_compiled_output_slice' -and
    [string]$report.post_effect_tonemap.exact_equations.exposure_multiplier -eq
        'exp(fp_c3[114].w)' -and
    [string]$report.post_effect_tonemap.exact_equations.output_transfer -eq
        'piecewise_srgb_oetf' -and
    @($report.post_effect_tonemap.unavailable_runtime_values).Count -eq 5) (
    'Promoted Z-A receive-shadow or tone-map boundary changed.')

Write-Host 'Z-A scene/color boundary workflow contract passed.'
