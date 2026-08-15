param()

$ErrorActionPreference = 'Stop'
$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$analyzer = Join-Path $PSScriptRoot 'analyze_sv_fresnel_effect_static_material.py'
$promoted = Join-Path $gameRoot (
    'docs\kanto\evidence\sv_fresnel_effect_static_material_report.json')

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

Assert-Condition (Test-Path -LiteralPath $analyzer -PathType Leaf) (
    'SV FresnelEffect static analyzer is missing.')
$source = Get-Content -LiteralPath $analyzer -Raw
foreach ($token in @(
        'runtime_execution": False',
        'emulator_used": False',
        'kNativeFresnelEffectMaterialMode',
        'BaseColorMap1',
        'fp_t_tcb_18',
        'fp_t_tcb_34',
        'pow(angle_term, 5)',
        'shared neutral environment')) {
    Assert-Condition ($source.Contains($token)) (
        "Static analyzer is missing contract token: $token")
}

Assert-Condition (Test-Path -LiteralPath $promoted -PathType Leaf) (
    'Promoted SV FresnelEffect static report is missing.')
$report = Get-Content -LiteralPath $promoted -Raw | ConvertFrom-Json
Assert-Condition ([string]$report.schema -eq
    'pokemon-autochess-sv-fresnel-effect-static-material-evidence-v1') (
    'Promoted FresnelEffect evidence has the wrong schema.')
Assert-Condition (-not [bool]$report.method.runtime_execution -and
    -not [bool]$report.method.emulator_used) (
    'Promoted FresnelEffect evidence must remain emulator-free.')
Assert-Condition ([int]$report.summary.materials_checked -eq 4 -and
    [int]$report.summary.runtime_material_mode -eq 34 -and
    [int]$report.program.variation_index -eq 0 -and
    [string]$report.program.shader_key_hex -eq '0x59' -and
    [string]$report.program.global_key_hex -eq '0x0') (
    'Promoted FresnelEffect program/material coverage changed.')
Assert-Condition ([string]$report.constant_mappings.FresnelAlphaMin -eq
    'fp_c7.data[25].x' -and
    [string]$report.constant_mappings.FresnelAlphaMax -eq
    'fp_c7.data[25].y' -and
    [string]$report.constant_mappings.FresnelAngleBias -eq
    'fp_c7.data[50].z' -and
    [string]$report.constant_mappings.LocalSpecularProbeIntensity -eq
    'fp_c7.data[64].x') (
    'Promoted FresnelEffect constant mapping changed.')
Assert-Condition ([string]$report.runtime_bridge.secondary_map_slot -eq
    'emissive_texture_linear' -and
    [string]$report.runtime_bridge.local_probe_substitute -eq
    'shared neutral environment' -and
    @($report.runtime_bridge.backends).Count -eq 3) (
    'Promoted FresnelEffect runtime boundary changed.')

Write-Output '[SvFresnelEffectStaticMaterialWorkflowTest] PASS'
