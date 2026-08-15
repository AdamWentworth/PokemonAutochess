param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$analyzer = Join-Path $PSScriptRoot 'analyze_za_eye_static_material.py'
$promoted = Join-Path $gameRoot (
    'docs\kanto\evidence\za_eye_static_material_report.json')

Assert-Condition (Test-Path -LiteralPath $analyzer -PathType Leaf) (
    'Z-A Eye static analyzer is missing.')
$source = Get-Content -LiteralPath $analyzer -Raw
foreach ($token in @(
        'runtime_execution": False',
        'emulator_used": False',
        'fp_t_tcb_8',
        'fp_t_tcb_16',
        'fp_t_tcb_C',
        'fp_t_tcb_1E')) {
    Assert-Condition ($source.Contains($token)) (
        "Z-A Eye analyzer lost contract token: $token")
}

Assert-Condition (Test-Path -LiteralPath $promoted -PathType Leaf) (
    'Promoted Z-A Eye static report is missing.')
$report = Get-Content -LiteralPath $promoted -Raw | ConvertFrom-Json
Assert-Condition ([string]$report.schema -eq
    'pokemon-autochess-za-eye-static-material-evidence-v1') (
    'Promoted Z-A Eye evidence has the wrong schema.')
Assert-Condition (-not [bool]$report.method.runtime_execution -and
    -not [bool]$report.method.emulator_used) (
    'Promoted Z-A Eye evidence must remain emulator-free.')
Assert-Condition ([int]$report.summary.models -eq 4 -and
    [int]$report.summary.materials -eq 8 -and
    [int]$report.summary.selected_variation -eq 146 -and
    [int]$report.summary.mapped_texture_roles -eq 4 -and
    [int]$report.summary.undecoded_authored_textures -eq 0) (
    'Promoted Z-A Eye coverage changed.')

Write-Host 'Z-A Eye static-material workflow contract passed.'
