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
        'EnableHairSpecular', 'fp_t_tcb_1A',
        'runtime_execution": False', 'emulator_used": False')) {
    Assert-Condition ($source.Contains($token)) (
        "Z-A IkCharacter dataflow analyzer lost contract token: $token")
}

Assert-Condition (Test-Path -LiteralPath $promoted -PathType Leaf) (
    'Promoted Z-A IkCharacter dataflow report is missing.')
$report = Get-Content -LiteralPath $promoted -Raw | ConvertFrom-Json
Assert-Condition ([string]$report.schema -eq
    'pokemon-autochess-za-ik-character-dataflow-evidence-v1') (
    'Promoted Z-A IkCharacter dataflow evidence has the wrong schema.')
Assert-Condition (-not [bool]$report.method.runtime_execution -and
    -not [bool]$report.method.emulator_used) (
    'Promoted Z-A IkCharacter dataflow evidence must remain emulator-free.')
Assert-Condition ([int]$report.summary.selected_programs -eq 4 -and
    [int]$report.summary.selected_materials -eq 222 -and
    [int]$report.summary.output_reachable_body_resources -eq 13 -and
    [int]$report.summary.hair_specular_enabled_materials -eq 0 -and
    [int]$report.summary.hair_specular_single_option_differentials -eq 3 -and
    [int]$report.summary.mapped_eye_material_fields -eq 7) (
    'Promoted Z-A IkCharacter dataflow coverage changed.')
Assert-Condition (
    [string]$report.shared_material_buffer_mappings.NormalHeight -eq
        'fp_c7[4].z' -and
    [string]$report.shared_material_buffer_mappings.LayerMaskScale4 -eq
        'fp_c7[9].x' -and
    [string]$report.shared_material_buffer_mappings.BaseColorLayer4 -eq
        'fp_c8[13].xyzw') (
    'Promoted shared Z-A material-buffer mapping changed.')
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

Write-Host 'Z-A IkCharacter compiled-dataflow workflow contract passed.'
