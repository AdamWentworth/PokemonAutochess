param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$analyzer = Join-Path $PSScriptRoot 'analyze_za_kanto_option_dataflow.py'
$promoted = Join-Path $gameRoot (
    'docs\kanto\evidence\za_kanto_option_dataflow.json')

Assert-Condition (Test-Path -LiteralPath $analyzer -PathType Leaf) (
    'Z-A option-dataflow analyzer is missing.')
$source = Get-Content -LiteralPath $analyzer -Raw
foreach ($token in @(
        'complete_exact_single_option_graph',
        'compiled_output_dependency_differentials',
        'same_resources_changed_output_equations',
        'runtime_execution": False', 'emulator_used": False')) {
    Assert-Condition ($source.Contains($token)) (
        "Z-A option-dataflow analyzer lost contract token: $token")
}

Assert-Condition (Test-Path -LiteralPath $promoted -PathType Leaf) (
    'Promoted Z-A option-dataflow report is missing.')
$report = Get-Content -LiteralPath $promoted -Raw | ConvertFrom-Json
Assert-Condition ([string]$report.schema -eq
    'pokemon-autochess-za-kanto-option-dataflow-evidence-v1') (
    'Promoted Z-A option-dataflow evidence has the wrong schema.')
Assert-Condition (-not [bool]$report.method.runtime_execution -and
    -not [bool]$report.method.emulator_used) (
    'Promoted Z-A option-dataflow evidence must remain emulator-free.')
Assert-Condition ([int]$report.summary.programs_analyzed -eq 133 -and
    [int]$report.summary.stages_analyzed -eq 266 -and
    [int]$report.summary.one_option_edges -eq 144 -and
    [int]$report.summary.shader_families -eq 3 -and
    [int]$report.summary.covered_options -eq 36) (
    'Promoted Z-A option-dataflow coverage changed.')

$hair = @($report.option_impacts | Where-Object {
    $_.shader_family -eq 'IkCharacter' -and
    $_.changed_option -eq 'EnableHairSpecular' })
Assert-Condition ($hair.Count -eq 1 -and [int]$hair[0].edge_count -eq 3 -and
    $hair[0].added_output_samplers -contains 'fp_t_tcb_1A') (
    'Z-A HairSpecular output dependency changed.')
$displacement = @($report.option_impacts | Where-Object {
    $_.shader_family -eq 'IkCharacter' -and
    $_.changed_option -eq 'EnableDisplacementMap' })
Assert-Condition ($displacement.Count -eq 1 -and
    [int]$displacement[0].selected_program_endpoint_edges -eq 1 -and
    $displacement[0].added_output_samplers -contains 'vp_t_tcb_24') (
    'Z-A displacement output dependency changed.')
$highlight = @($report.option_impacts | Where-Object {
    $_.shader_family -eq 'Eye' -and
    $_.changed_option -eq 'EnableHighlight' })
Assert-Condition ($highlight.Count -eq 1 -and
    $highlight[0].added_output_samplers -contains 'fp_t_tcb_1E') (
    'Z-A eye-highlight output dependency changed.')
$receiveShadow = @($report.option_impacts | Where-Object {
    $_.shader_family -eq 'IkCharacter' -and
    $_.changed_option -eq 'ReceiveShadow' })
Assert-Condition ($receiveShadow.Count -eq 1 -and
    [int]$receiveShadow[0].selected_program_endpoint_edges -eq 3 -and
    [int]$receiveShadow[0].fragment_classifications.identical_compiled_output_slice -eq 3) (
    'Z-A ReceiveShadow compiled-output identity changed.')

Write-Host 'Z-A complete option-dataflow workflow contract passed.'
