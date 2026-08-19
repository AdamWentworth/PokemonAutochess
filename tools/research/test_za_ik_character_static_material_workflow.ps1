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
        'ikcharacter_eye_scene_boundary',
        'linearToSrgb(emissionLuminance)',
        'bodyEmission',
        'packIkCharacterEmissionColor', 'zaIkEmissionColor',
        'hair_specular_enabled',
        'zaIkRimPresentationScale',
        'rimShape',
        'halfLambertBiasSquared',
        'shadowProcessArea',
        'baseToMidHue',
        'environmentRadiance * sourceAlbedo * metallic',
        'BaseColorDarkness', 'SpecularMaskMapValue',
        'CachedTextureRgba shadowingColorMask',
        'layerWeightSum', '1.0f - baseEmissionIntensity',
        '1.0f - layerEmissionIntensities[layer]',
        'shadowingGiGain', 'shadowAmount * shadowingGiGain',
        'normalDetailDelta',
        'machop_source_canary', 'smooth_matte_ikcharacter',
        'mega_gengar_upward_noise', 'rim_composite_scale')) {
    Assert-Condition ($source.Contains($token)) (
        "Z-A IkCharacter analyzer lost contract token: $token")
}

Assert-Condition (Test-Path -LiteralPath $promoted -PathType Leaf) (
    'Promoted Z-A IkCharacter static report is missing.')
$report = Get-Content -LiteralPath $promoted -Raw | ConvertFrom-Json
Assert-Condition ([string]$report.schema -eq
    'pokemon-autochess-za-ik-character-static-material-evidence-v2') (
    'Promoted Z-A IkCharacter report has the wrong schema.')
Assert-Condition (-not [bool]$report.method.runtime_execution -and
    -not [bool]$report.method.emulator_used) (
    'Promoted Z-A IkCharacter evidence must remain emulator-free.')
Assert-Condition ([int]$report.summary.selected_models -eq 212 -and
    [int]$report.summary.materials -eq 1036 -and
    [int]$report.summary.selected_programs -eq 5 -and
    [int]$report.summary.texture_roles -eq 14 -and
    [int]$report.summary.undecoded_authored_textures -eq 0 -and
    [int]$report.summary.complete_option_graph_edges -eq 183 -and
    [int]$report.summary.ikcharacter_eye_materials -eq 428 -and
    [int]$report.summary.consumed_ikcharacter_eye_texture_bindings -eq 4896 -and
    [int]$report.summary.unconsumed_ikcharacter_eye_texture_bindings -eq 0 -and
    [int]$report.summary.cooked_phmat_files_verified -eq 212 -and
    [int]$report.summary.cooked_mode32_submesh_records_verified -gt 0 -and
    [int]$report.summary.cooked_mode32_native_parameter_records_verified -eq
        [int]$report.summary.cooked_mode32_submesh_records_verified -and
    [int]$report.summary.cooked_body_emission_records_verified -eq 4 -and
    [int]$report.summary.cooked_neutral_hair_auxiliary_records_verified -eq
        [int]$report.summary.cooked_mode32_submesh_records_verified -and
    [int]$report.summary.machop_source_material_records_verified -eq 6 -and
    [int]$report.summary.machop_cooked_material_records_verified -eq 6 -and
    [int]$report.summary.machop_cooked_zero_specular_records_verified -eq 6 -and
    [int]$report.summary.shadowing_gi_gain_runtime_backends -eq 3 -and
    [int]$report.summary.hair_specular_enabled_materials -eq 0 -and
    [int]$report.summary.mapped_body_material_fields -eq 64 -and
    [int]$report.summary.exact_final_scene_fade_programs -eq 5 -and
    [int]$report.summary.backends_bridged -eq 3) (
    'Promoted Z-A IkCharacter corpus coverage changed.')
Assert-Condition ([int]$report.summary.material_classes.core_body -eq 604 -and
    [int]$report.summary.material_classes.displacement -eq 4 -and
    [int]$report.summary.material_classes.eye_options -eq 428) (
    'Promoted Z-A IkCharacter material classes changed.')
Assert-Condition (@($report.machop_source_canary).Count -eq 2 -and
    @($report.machop_source_canary | Where-Object {
        [string]$_.source_surface_classification -eq
            'smooth_matte_ikcharacter_without_roughness_or_hair_lobe' -and
        @($_.material_partition).Count -eq 3 -and
        @($_.body_texture_roles).Count -eq 9 -and
        @($_.eye_texture_roles).Count -eq 11
    }).Count -eq 2) (
    'Z-A Machop source canary changed.')
Assert-Condition (@($report.remaining_equation_gaps).Count -eq 5 -and
    @($report.remaining_equation_gaps | Where-Object {
        [string]$_.id -eq 'mega_gengar_upward_noise'
    }).Count -eq 1) (
    'Z-A IkCharacter report must preserve its explicit equation gaps.')

Write-Host 'Z-A IkCharacter static-material workflow contract passed.'
