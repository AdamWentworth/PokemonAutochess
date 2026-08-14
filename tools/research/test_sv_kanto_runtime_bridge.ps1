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
$selectedProgramAbi = Join-Path $gameRoot (
    'docs\kanto\evidence\sv_kanto_selected_program_abi.json')
$eyeStaticEvidence = Join-Path $gameRoot (
    'docs\kanto\evidence\sv_eevee_static_material_report.json')
$promoted = Join-Path $gameRoot (
    'docs\kanto\evidence\sv_kanto_runtime_bridge.json')
$temporary = Join-Path ([IO.Path]::GetTempPath()) (
    'pokemonautochess-sv-runtime-bridge-' + [Guid]::NewGuid().ToString('N') + '.json')

try {
    & python $analyzer `
        --game-root $gameRoot `
        --differential-evidence $differentials `
        --selected-program-abi $selectedProgramAbi `
        --eye-static-evidence $eyeStaticEvidence `
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
    Assert-Condition ([int]$report.summary.proven_mapping_count -eq 7 -and
        [int]$report.summary.proven_material_bindings_checked -eq 936 -and
        [int]$report.summary.exact_runtime_translations -eq 936 -and
        [int]$report.summary.runtime_translation_mismatches -eq 0) (
        'SV Kanto proven runtime binding coverage changed.')
    Assert-Condition (
        [int]$report.eye_clear_coat_normal_transport.selected_program_count -eq 4 -and
        [int]$report.eye_clear_coat_normal_transport.material_count -eq 486 -and
        [string]$report.eye_clear_coat_normal_transport.source_role -eq 'NormalMap1' -and
        [string]$report.eye_clear_coat_normal_transport.source_scale_parameter -eq 'NormalHeight1') (
        'SV EyeClearCoat source-normal bridge changed.')
    Assert-Condition ([int]$report.summary.sss_materials_checked -eq 392 -and
        [int]$report.summary.sss_complete_texture_stacks -eq 392 -and
        [int]$report.summary.sss_neutral_mask_transforms -eq 392) (
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
