param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$analyzer = Join-Path $PSScriptRoot 'audit_sv_kanto_runtime_bridge.py'
$differentials = Join-Path $gameRoot (
    'docs\kanto\evidence\sv_kanto_program_differentials.json')
$promoted = Join-Path $gameRoot (
    'docs\kanto\evidence\sv_kanto_runtime_bridge.json')
$temporary = Join-Path ([IO.Path]::GetTempPath()) (
    'pokemonautochess-sv-runtime-bridge-' + [Guid]::NewGuid().ToString('N') + '.json')

try {
    & python $analyzer `
        --game-root $gameRoot `
        --differential-evidence $differentials `
        --output $temporary
    Assert-Condition ($LASTEXITCODE -eq 0) (
        "SV Kanto runtime bridge audit failed with exit code $LASTEXITCODE")
    $report = Get-Content -LiteralPath $temporary -Raw | ConvertFrom-Json
    Assert-Condition ([string]$report.schema -eq
        'pokemon-autochess-sv-kanto-runtime-bridge-audit-v1') (
        'SV Kanto runtime bridge report has the wrong schema.')
    Assert-Condition (-not [bool]$report.method.runtime_execution -and
        -not [bool]$report.method.emulator_used) (
        'SV Kanto runtime bridge audit must remain emulator-free.')
    Assert-Condition ([int]$report.summary.proven_mapping_count -eq 6 -and
        [int]$report.summary.proven_material_bindings_checked -eq 348 -and
        [int]$report.summary.exact_runtime_translations -eq 348 -and
        [int]$report.summary.runtime_translation_mismatches -eq 0) (
        'SV Kanto proven runtime binding coverage changed.')
    Assert-Condition ([int]$report.summary.sss_materials_checked -eq 308 -and
        [int]$report.summary.sss_complete_texture_stacks -eq 308 -and
        [int]$report.summary.sss_neutral_mask_transforms -eq 308) (
        'SV Kanto SSS transport coverage changed.')
    Assert-Condition (Test-Path -LiteralPath $promoted -PathType Leaf) (
        'Promoted SV Kanto runtime bridge evidence is missing.')
    $promotedReport = Get-Content -LiteralPath $promoted -Raw |
        ConvertFrom-Json
    Assert-Condition ([int]$promotedReport.summary.exact_runtime_translations -eq
        [int]$report.summary.exact_runtime_translations -and
        [int]$promotedReport.summary.sss_materials_checked -eq
        [int]$report.summary.sss_materials_checked) (
        'Promoted SV Kanto runtime bridge evidence is stale.')
    Write-Output '[SvKantoRuntimeBridgeTest] PASS'
} finally {
    Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
}
