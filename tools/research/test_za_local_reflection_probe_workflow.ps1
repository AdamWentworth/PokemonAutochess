param(
    [string]$EngineRoot = "D:\Projects\Phlosion\PhlosionEngine"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$analyzer = Join-Path $PSScriptRoot 'analyze_za_local_reflection_probe.py'
$promoted = Join-Path $gameRoot (
    'docs\kanto\evidence\za_local_reflection_static_report.json')

Assert-Condition (Test-Path -LiteralPath $analyzer -PathType Leaf) (
    'Z-A local-reflection analyzer is missing.')
$source = Get-Content -LiteralPath $analyzer -Raw
foreach ($token in @(
        'phlosion-za-local-reflection-rgba16f-cube-mips-packed-v1',
        'runtime_execution": False',
        'emulator_used": False',
        'source_bntx_decode_plus_manifest_transport_plus_backend_contract',
        'mip-major, then +X,-X,+Y,-Y,+Z,-Z',
        'zaIkLocalReflectionDirection',
        'reflect(-viewDirection, mappedNormal)')) {
    Assert-Condition ($source.Contains($token)) (
        "Z-A local-reflection analyzer lost contract token: $token")
}

Assert-Condition (Test-Path -LiteralPath $promoted -PathType Leaf) (
    'Promoted Z-A local-reflection report is missing.')
$report = Get-Content -LiteralPath $promoted -Raw | ConvertFrom-Json
Assert-Condition (
    [string]$report.schema -eq (
        'pokemon-autochess-za-local-reflection-static-report-v2') -and
    [bool]$report.summary.all_bindings_decoded -and
    [int]$report.summary.backends_bridged -eq 3 -and
    [int]$report.summary.selected_models -eq 52 -and
    [int]$report.summary.unique_source_probes -eq 1) (
    'Promoted Z-A local-reflection corpus summary changed.')
Assert-Condition (
    [int]$report.unique_probes[0].face_size -eq 128 -and
    [int]$report.unique_probes[0].mip_count -eq 8 -and
    [string]$report.unique_probes[0].source_format -eq (
        'BNTX-0x1F05 / BC6H_UF16')) (
    'Promoted Z-A local-reflection source topology changed.')
Assert-Condition (
    [string]$report.transport.runtime_direction -eq
        'reflect(-view, mapped_normal); no diffuse-cube Z flip') (
    'Promoted Z-A local-reflection direction changed.')

Write-Host 'Z-A local-reflection workflow contract passed.'
