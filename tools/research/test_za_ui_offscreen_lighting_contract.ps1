[CmdletBinding()]
param(
    [string]$GameRoot = '',
    [string]$EngineRoot = 'D:\Projects\Phlosion\PhlosionEngine'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $scriptRoot '..\..'
}
$GameRoot = [IO.Path]::GetFullPath($GameRoot)
$EngineRoot = [IO.Path]::GetFullPath($EngineRoot)

$diffuse = Get-Content -Raw (Join-Path $GameRoot 'docs\kanto\evidence\za_ui_offscreen_diffuse_probe.json') | ConvertFrom-Json
$specular = Get-Content -Raw (Join-Path $GameRoot 'docs\kanto\evidence\za_ui_offscreen_specular_probe.json') | ConvertFrom-Json
$light = Get-Content -Raw (Join-Path $GameRoot 'docs\kanto\evidence\za_ui_offscreen_light.json') | ConvertFrom-Json

Assert-Condition ($diffuse.source_sha256 -eq '0c5241a6cb7121f857e174fe40bac9cc12ea4966ea6df410d2a62232cfc26c49') 'Diffuse source hash drifted.'
Assert-Condition ($diffuse.cube_face_size -eq 64 -and $diffuse.source_mip_count -eq 1) 'Diffuse cube topology drifted.'
Assert-Condition ($diffuse.decoded_width -eq 384 -and $diffuse.decoded_height -eq 128) 'Diffuse carrier topology drifted.'
Assert-Condition ($diffuse.decoded_channel_min[0] -gt 0.0045 -and $diffuse.decoded_channel_max[0] -lt 0.0052) 'Diffuse HDR range drifted.'
Assert-Condition ($specular.source_sha256 -eq '79699294c5792276ba4a95fce7966273a0c384de6130df0ef24084cee81a20e8') 'Specular source hash drifted.'
Assert-Condition ($specular.cube_face_size -eq 64 -and $specular.source_mip_count -eq 7) 'Specular cube topology drifted.'
Assert-Condition ($specular.decoded_width -eq 384 -and $specular.decoded_height -eq 254) 'Specular carrier topology drifted.'
Assert-Condition ($light.source_sha256 -eq '62218712331b7379221dfca55f790ae89e7513b44a2c3e95f66ee8e9340f742e') 'UI-light source hash drifted.'
Assert-Condition ($light.component_count -eq 24) 'UI-light component count drifted.'

$directional = @($light.components | Where-Object name -eq 'DirectionalMain')
$global = @($light.components | Where-Object name -eq 'Global')
$probe = @($light.components | Where-Object name -eq 'ProbeMain')
$category6 = @($light.components | Where-Object name -eq 'dir_6')
$rim6 = @($light.components | Where-Object name -eq 'rim_6')
Assert-Condition ($directional.Count -eq 1 -and $directional[0].transform.rotation_euler_radians[0] -lt -0.70) 'DirectionalMain transform is unavailable.'
Assert-Condition ($global.Count -eq 1 -and $global[0].scalars.Gamma -eq 1.0 -and $global[0].scalars.TMScale -eq 1.0) 'Global exposure contract drifted.'
Assert-Condition ($probe.Count -eq 1 -and $probe[0].scalars.DiffuseProbeIntensity -eq 1.0 -and $probe[0].scalars.SpecularProbeIntensity -eq 1.0) 'ProbeMain intensity contract drifted.'
Assert-Condition ($category6.Count -eq 1 -and [Math]::Abs($category6[0].scalars.Intensity - 4.2) -lt 0.001) 'Category-6 direct intensity drifted.'
Assert-Condition ($rim6.Count -eq 1 -and $rim6[0].vectors.Color[0] -eq 0.0) 'Category-6 rim contract drifted.'

$gamePreview = Get-Content -Raw (Join-Path $GameRoot 'src\game\editor\PokemonPrefabPreview.cpp')
$nativeCooker = Get-Content -Raw (Join-Path $GameRoot 'tools\PhlosionNativeModelIr.cpp')
$materialCache = Get-Content -Raw (Join-Path $GameRoot (
    'src\game\runtime\shared\projected\backend_mesh\' +
    'SharedProjectedUnitBackendMeshMaterialTemplateCache.cpp'))
$vulkanMaterial = Get-Content -Raw (Join-Path $EngineRoot 'assets\shaders\vulkan\world_material.glsl')
$vulkanWorld = Get-Content -Raw (Join-Path $EngineRoot 'assets\shaders\vulkan\world.frag')
$vulkanWorldIndirect = Get-Content -Raw (Join-Path $EngineRoot 'assets\shaders\vulkan\world_indirect.frag')
$d3dMaterial = Get-Content -Raw (Join-Path $EngineRoot 'src\engine\render\d3d12\D3D12RenderBackendWorldPipeline.cpp')
$glMaterial = Get-Content -Raw (Join-Path $EngineRoot 'src\engine\render\opengl\OpenGLRenderBackendWorldPipeline.cpp')
foreach ($source in @($vulkanMaterial, $d3dMaterial, $glMaterial)) {
    Assert-Condition ($source.Contains('zaUiDirectIntensity')) 'A renderer is missing source direct-light decoding.'
    Assert-Condition (
        $source.Contains('-0.44695543') -and
        $source.Contains('0.64944804') -and
        $source.Contains('-0.61518134')) (
        'A renderer is missing the source-stage front-key light direction.')
    Assert-Condition ($source.Contains('zaUiGiIntensity')) 'A renderer is missing source GI-category decoding.'
    Assert-Condition ($source.Contains('0.006')) 'A renderer is missing the proven diffuse-carrier bound.'
    Assert-Condition ($source.Contains('96.0')) 'A renderer is missing the reviewed Source Stage diffuse exposure.'
    Assert-Condition ($source.Contains('fract(packedCategoryAndFlags)') -or
                      $source.Contains('frac(packedCategoryAndFlags)')) (
        'A renderer cannot recover a lighting category carried beside special material flags.')
}
Assert-Condition ($gamePreview.Contains('probemain_diffuse.png') -and $gamePreview.Contains('probemain_specular.png')) 'Inspector source-probe binding is missing.'
Assert-Condition ($gamePreview.Contains(
    'batch.lightProjectionTextureRgba = diffuse->rgba.data()')) (
    'The source diffuse probe is not using the cross-backend light-projection carrier.')
Assert-Condition ($gamePreview.Contains('resolvedMaterialBatch(batch)')) (
    'The source probe attachment does not resolve shared material templates.')
Assert-Condition ($vulkanMaterial.Contains('sampler2D zaUiDiffuseProbeMap') -and
                  $vulkanWorld.Contains('lightProjectionTexture,') -and
                  $vulkanWorldIndirect.Contains(
                      'lightProjectionTextures[nonuniformEXT(materialIndex)],')) (
    'Vulkan is not sampling the source diffuse probe from its stable carrier.')
Assert-Condition ($vulkanWorld.Contains('zaSourceStageCarrier') -and
                  $vulkanWorldIndirect.Contains('zaSourceStageCarrier')) (
    'Vulkan is missing its source-probe profile marker on a world path.')
Assert-Condition ($d3dMaterial.Contains('gLightProjectionTex') -and
                  $glMaterial.Contains('uLightProjectionTexture')) (
    'D3D12 or OpenGL is not sampling the source diffuse probe from its stable carrier.')
Assert-Condition ($nativeCooker.Contains('resolvedMaterialFlags >= 0.5f') -and
                  $nativeCooker.Contains('nativeLightingCategory / 16.0f')) (
    'The native cooker is not preserving special CPU flags beside the Z-A category.')
Assert-Condition ($materialCache.Contains(
    'material.lightProjectionUvRowV[3] = material.materialFlags')) (
    'The runtime is not carrying the packed Z-A category into renderer material data.')

Write-Host 'Z-A UI off-screen lighting contract passed.'
