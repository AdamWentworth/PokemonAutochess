[CmdletBinding()]
param(
    [string]$SourcePackage = (
        '\\TNAS-98B9\pokemon\Game Files\Switch\' +
        'Pokemon_Legends_ZA_v2.0.0_Merged_GameFiles\arc\' +
        'lightsplspl_ui_offscreen_pokespl_ui_offscreen_poke.trlgt.trpak'),
    [string]$GameRoot = '',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-FullPath([string]$PathValue) {
    return [IO.Path]::GetFullPath($PathValue)
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $scriptRoot '..\..'
}
$GameRoot = Resolve-FullPath $GameRoot
$SourcePackage = Resolve-FullPath $SourcePackage
if (-not (Test-Path -LiteralPath $SourcePackage -PathType Container)) {
    throw "Extracted Z-A UI-light package is missing: $SourcePackage"
}

$diffuseSource = Join-Path $SourcePackage '0000 - 1AAC7E7FB7EF88CD.bntx'
$specularSource = Join-Path $SourcePackage '0001 - C1489DA5BE4D9C9A.bntx'
$lightSource = Join-Path $SourcePackage '0002 - E98DD4847BDE062E.bin'
foreach ($required in @($diffuseSource, $specularSource, $lightSource)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required Z-A UI-light source is missing: $required"
    }
}

$exporter = Join-Path $scriptRoot 'ZaUiOffscreenProbeExporter\ZaUiOffscreenProbeExporter.csproj'
$analyzer = Join-Path $scriptRoot 'analyze_za_ui_offscreen_light.py'
$runtimeRoot = Join-Path $GameRoot 'assets\textures\source\za\ui_offscreen_poke'
$evidenceRoot = Join-Path $GameRoot 'docs\kanto\evidence'
New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null

if (-not $SkipBuild) {
    dotnet build $exporter -c Release --nologo
    if ($LASTEXITCODE -ne 0) { throw 'Z-A UI probe exporter build failed.' }
}

$exporterDll = Join-Path $scriptRoot (
    'ZaUiOffscreenProbeExporter\bin\Release\net8.0\' +
    'ZaUiOffscreenProbeExporter.dll')
if (-not (Test-Path -LiteralPath $exporterDll -PathType Leaf)) {
    throw "Z-A UI probe exporter is missing: $exporterDll"
}

& dotnet $exporterDll `
    --source $diffuseSource `
    --output (Join-Path $runtimeRoot 'probemain_diffuse.png') `
    --manifest (Join-Path $evidenceRoot 'za_ui_offscreen_diffuse_probe.json')
if ($LASTEXITCODE -ne 0) { throw 'Diffuse-probe export failed.' }

& dotnet $exporterDll `
    --source $specularSource `
    --output (Join-Path $runtimeRoot 'probemain_specular.png') `
    --manifest (Join-Path $evidenceRoot 'za_ui_offscreen_specular_probe.json')
if ($LASTEXITCODE -ne 0) { throw 'Specular-probe export failed.' }

& python $analyzer `
    --source $lightSource `
    --output (Join-Path $evidenceRoot 'za_ui_offscreen_light.json')
if ($LASTEXITCODE -ne 0) { throw 'Z-A UI-light analysis failed.' }

Write-Host 'Exported the exact Z-A off-screen Pokemon light and HDR probes.'
Write-Host "  Runtime probe carriers: $runtimeRoot"
Write-Host "  Reproducible evidence: $evidenceRoot"
