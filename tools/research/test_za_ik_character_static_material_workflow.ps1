param(
    [string]$EngineRoot = "D:\Projects\Phlosion\PhlosionEngine"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$analyzer = Join-Path $PSScriptRoot (
    'analyze_za_ik_character_static_material.py')
$promoted = Join-Path $gameRoot (
    'docs\kanto\evidence\za_ik_character_static_material_report.json')

Assert-Condition (Test-Path -LiteralPath $analyzer -PathType Leaf) (
    'Z-A IkCharacter static analyzer is missing.')
$source = Get-Content -LiteralPath $analyzer -Raw
foreach ($token in @(
        'runtime_execution": False',
        'emulator_used": False',
        'fp_t_tcb_1C',
        'vp_t_tcb_24',
        'complete_ikcharacter_brdf_order',
        'fibre_feather_response',
        'rim_composite_scale')) {
    Assert-Condition ($source.Contains($token)) (
        "Z-A IkCharacter analyzer lost contract token: $token")
}

Assert-Condition (Test-Path -LiteralPath $promoted -PathType Leaf) (
    'Promoted Z-A IkCharacter static report is missing.')
$report = Get-Content -LiteralPath $promoted -Raw | ConvertFrom-Json
Assert-Condition ([string]$report.schema -eq
    'pokemon-autochess-za-ik-character-static-material-evidence-v1') (
    'Promoted Z-A IkCharacter report has the wrong schema.')
Assert-Condition (-not [bool]$report.method.runtime_execution -and
    -not [bool]$report.method.emulator_used) (
    'Promoted Z-A IkCharacter evidence must remain emulator-free.')
Assert-Condition ([int]$report.summary.selected_models -eq 52 -and
    [int]$report.summary.materials -eq 222 -and
    [int]$report.summary.selected_programs -eq 4 -and
    [int]$report.summary.texture_roles -eq 13 -and
    [int]$report.summary.undecoded_authored_textures -eq 0 -and
    [int]$report.summary.complete_option_graph_edges -eq 144 -and
    [int]$report.summary.backends_bridged -eq 3) (
    'Promoted Z-A IkCharacter corpus coverage changed.')
Assert-Condition ([int]$report.summary.material_classes.core_body -eq 140 -and
    [int]$report.summary.material_classes.displacement -eq 2 -and
    [int]$report.summary.material_classes.eye_options -eq 80) (
    'Promoted Z-A IkCharacter material classes changed.')
Assert-Condition (@($report.remaining_equation_gaps).Count -eq 4) (
    'Z-A IkCharacter report must preserve its explicit equation gaps.')

Write-Host 'Z-A IkCharacter static-material workflow contract passed.'
