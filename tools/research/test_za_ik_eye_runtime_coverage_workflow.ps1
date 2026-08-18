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
        'bakeIkCharacterEyeColorComposite',
        'kNativeIkCharacterEyeMaterialMode',
        'resolveZaIkEyeParallaxUv',
        'phmat_material_modes', 'cooked_mode35_submesh_records',
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
    [int]$report.summary.selected_models_with_ikcharacter_eyes -eq 194 -and
    [int]$report.summary.selected_eye_materials -eq 428 -and
    [int]$report.summary.authored_texture_bindings -eq 4896 -and
    [int]$report.summary.consumed_texture_bindings -eq 4896 -and
    [int]$report.summary.unconsumed_texture_bindings -eq 0 -and
    [int]$report.summary.materials_with_nonzero_parallax_height -eq 366 -and
    [int]$report.summary.materials_with_nonunit_parallax_ior -eq 6 -and
    [int]$report.summary.materials_with_eyelid_shadow_map -eq 188 -and
    [int]$report.summary.materials_with_nonzero_highlight_emission -eq 184 -and
    [int]$report.summary.materials_with_nonzero_specular -eq 302 -and
    [int]$report.summary.materials_with_nonzero_diffusion -eq 0 -and
    [int]$report.summary.materials_with_nonzero_rim_intensity -eq 0 -and
    [int]$report.summary.materials_with_nonzero_shadow_color_mask_value -eq 4 -and
    [int]$report.summary.cooked_phmat_files_verified -eq 194 -and
    [int]$report.summary.cooked_mode35_submesh_records -eq 428) (
    'Promoted Z-A IkCharacter eye coverage census changed.')
Assert-Condition (
    [string]$report.compiled_eye_material_buffer_mappings.ParallaxHeight -eq
        'fp_c7[5].y' -and
    [string]$report.compiled_eye_material_buffer_mappings.ParallaxIOR -eq
        'fp_c7[5].z' -and
    [string]$report.compiled_eye_material_buffer_mappings.EmissionIntensityLayer5 -eq
        'fp_c7[9].z' -and
    [string]$report.compiled_eye_material_buffer_mappings.BaseColorLayer6 -eq
        'fp_c8[15].xyzw' -and
    [string]$report.compiled_eye_material_buffer_mappings.EmissionColorLayer5 -eq
        'fp_c8[24].xyzw' -and
    [string]$report.compiled_eye_material_buffer_mappings.UVRotation2 -eq
        'fp_c7[21].y') (
    'Promoted Z-A IkCharacter eye buffer mapping changed.')
$parallax = @($report.texture_role_coverage | Where-Object {
    $_.role -eq 'ParallaxMap' })
$eyelid = @($report.texture_role_coverage | Where-Object {
    $_.role -eq 'EyelidShadowMaskMap' })
Assert-Condition ($parallax.Count -eq 1 -and $eyelid.Count -eq 1 -and
    [string]$parallax[0].status -like '*source-proven refraction*4-to-14*' -and
    [string]$eyelid[0].status -like '*BaseColorLayer6*') (
    'Promoted Z-A eye runtime boundary changed; update the coverage audit.')
Assert-Condition (
    [int]$report.runtime_bridge.selected_material_mode -eq 35 -and
    [string]$report.remaining_source_proven_runtime_target.id -eq
        'za_ikcharacter_scene_light_composition_boundary') (
    'Z-A eye coverage must preserve its mode-35 and remaining-parity boundary.')

Write-Host 'Z-A IkCharacter eye runtime-coverage workflow contract passed.'
