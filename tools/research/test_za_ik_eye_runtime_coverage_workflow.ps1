param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$analyzer = Join-Path $PSScriptRoot 'analyze_za_ik_eye_runtime_coverage.py'
$promoted = Join-Path $gameRoot (
    'docs\kanto\evidence\za_ik_eye_runtime_coverage.json')

Assert-Condition (Test-Path -LiteralPath $analyzer -PathType Leaf) (
    'Z-A IkCharacter eye coverage analyzer is missing.')
$source = Get-Content -LiteralPath $analyzer -Raw
foreach ($token in @(
        'exact_selected_material_census_plus_compiled_resource_',
        'ParallaxHeight', 'ParallaxIOR', 'EyelidShadowMaskMap',
        'headless_change_authorized',
        'runtime_execution": False', 'emulator_used": False')) {
    Assert-Condition ($source.Contains($token)) (
        "Z-A IkCharacter eye coverage analyzer lost token: $token")
}

Assert-Condition (Test-Path -LiteralPath $promoted -PathType Leaf) (
    'Promoted Z-A IkCharacter eye runtime coverage is missing.')
$report = Get-Content -LiteralPath $promoted -Raw | ConvertFrom-Json
Assert-Condition ([string]$report.schema -eq
    'pokemon-autochess-za-ik-eye-runtime-coverage-v1') (
    'Promoted Z-A IkCharacter eye coverage has the wrong schema.')
Assert-Condition (-not [bool]$report.method.runtime_execution -and
    -not [bool]$report.method.emulator_used) (
    'Promoted Z-A IkCharacter eye evidence must remain emulator-free.')
Assert-Condition (
    [int]$report.summary.selected_models_with_ikcharacter_eyes -eq 38 -and
    [int]$report.summary.selected_eye_materials -eq 80 -and
    [int]$report.summary.authored_texture_bindings -eq 928 -and
    [int]$report.summary.consumed_texture_bindings -eq 320 -and
    [int]$report.summary.unconsumed_texture_bindings -eq 608 -and
    [int]$report.summary.materials_with_nonzero_parallax_height -eq 70 -and
    [int]$report.summary.materials_with_eyelid_shadow_map -eq 48 -and
    [int]$report.summary.materials_with_nonzero_highlight_emission -eq 24) (
    'Promoted Z-A IkCharacter eye coverage census changed.')
Assert-Condition (
    [string]$report.compiled_eye_material_buffer_mappings.ParallaxHeight -eq
        'fp_c7[5].y' -and
    [string]$report.compiled_eye_material_buffer_mappings.ParallaxIOR -eq
        'fp_c7[5].z' -and
    [string]$report.compiled_eye_material_buffer_mappings.UVRotation2 -eq
        'fp_c7[21].y') (
    'Promoted Z-A IkCharacter eye buffer mapping changed.')
$parallax = @($report.texture_role_coverage | Where-Object {
    $_.role -eq 'ParallaxMap' })
$eyelid = @($report.texture_role_coverage | Where-Object {
    $_.role -eq 'EyelidShadowMaskMap' })
Assert-Condition ($parallax.Count -eq 1 -and $eyelid.Count -eq 1 -and
    [string]$parallax[0].status -eq 'retained_but_not_sampled_by_runtime' -and
    [string]$eyelid[0].status -eq 'retained_but_not_sampled_by_runtime') (
    'Promoted Z-A eye runtime boundary changed; update the coverage audit.')
Assert-Condition (-not [bool]$report.next_source_proven_runtime_target.
    headless_change_authorized) (
    'Z-A eye coverage must not silently authorize an unverified visual change.')

Write-Host 'Z-A IkCharacter eye runtime-coverage workflow contract passed.'
