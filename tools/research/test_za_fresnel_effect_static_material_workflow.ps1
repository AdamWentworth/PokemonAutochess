param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$analyzer = Join-Path $PSScriptRoot (
    'analyze_za_fresnel_effect_static_material.py')
$promoted = Join-Path $gameRoot (
    'docs\kanto\evidence\za_fresnel_effect_static_material_report.json')

Assert-Condition (Test-Path -LiteralPath $analyzer -PathType Leaf) (
    'Z-A FresnelEffect static analyzer is missing.')
$source = Get-Content -LiteralPath $analyzer -Raw
foreach ($token in @(
        'runtime_execution": False',
        'emulator_used": False',
        'kNativeFresnelEffectMaterialMode',
        '"NormalMap1"',
        'fp_t_tcb_18',
        'fp_t_tcb_34',
        'fp_c7.data[30].xy',
        'fp_c7.data[56].z',
        'fp_c7.data[70].x',
        '(nativeScarletSource || nativeZaSource)',
        'pow(angle_term, 5)')) {
    Assert-Condition ($source.Contains($token)) (
        "Z-A FresnelEffect analyzer lost contract token: $token")
}

Assert-Condition (Test-Path -LiteralPath $promoted -PathType Leaf) (
    'Promoted Z-A FresnelEffect static report is missing.')
$report = Get-Content -LiteralPath $promoted -Raw | ConvertFrom-Json
Assert-Condition ([string]$report.schema -eq
    'pokemon-autochess-za-fresnel-effect-static-material-evidence-v1') (
    'Promoted Z-A FresnelEffect evidence has the wrong schema.')
Assert-Condition (-not [bool]$report.method.runtime_execution -and
    -not [bool]$report.method.emulator_used) (
    'Promoted Z-A FresnelEffect evidence must remain emulator-free.')
Assert-Condition ([int]$report.summary.materials_checked -eq 4 -and
    [int]$report.summary.runtime_material_mode -eq 34 -and
    [int]$report.summary.remaining_undecoded_authored_resources -eq 0 -and
    [int]$report.program.variation_index -eq 0 -and
    [string]$report.program.shader_key_hex -eq '0x159') (
    'Promoted Z-A FresnelEffect program/material coverage changed.')
Assert-Condition (
    [string]$report.constant_mappings.Roughness -eq 'fp_c7.data[5].w' -and
    [string]$report.constant_mappings.FresnelAlphaMinMax -eq
        'fp_c7.data[30].xy' -and
    [string]$report.constant_mappings.FresnelAngleBias -eq
        'fp_c7.data[56].z' -and
    [string]$report.constant_mappings.LocalSpecularProbeIntensity -eq
        'fp_c7.data[70].x') (
    'Promoted Z-A FresnelEffect constant mapping changed.')
Assert-Condition (
    [string]$report.runtime_bridge.local_probe_slot -eq
        'environment_texture_linear_packed_rgba16f_cube' -and
    [string]$report.runtime_bridge.primary_normal_map_slot -eq
        'normal_texture_linear' -and
    [string]$report.runtime_bridge.secondary_normal_map_slot -eq
        'metallic_roughness_texture_linear_repurposed_in_mode_34' -and
    @($report.runtime_bridge.backends).Count -eq 3) (
    'Promoted Z-A FresnelEffect runtime bridge changed.')

Write-Host 'Z-A FresnelEffect static-material workflow contract passed.'
