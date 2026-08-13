[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ShaderStudyRoot,
    [Parameter(Mandatory = $true)]
    [string]$ExporterDll,
    [Parameter(Mandatory = $true)]
    [string]$ShaderDecoderExe,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-RequiredFile([string]$Value, [string]$Label) {
    $path = [IO.Path]::GetFullPath($Value)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "$Label is missing: $path"
    }
    return $path
}

function Export-PermutationSet(
    [string]$Archive,
    [string]$OutputDirectory,
    [int[]]$Variations) {
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    foreach ($variation in $Variations) {
        $prefix = Join-Path $OutputDirectory ('v{0:D3}' -f $variation)
        $fragmentGlsl = "$prefix.fsh.maxwell.glsl"
        $vertexGlsl = "$prefix.vsh.maxwell.glsl"
        if ((Test-Path -LiteralPath $fragmentGlsl -PathType Leaf) -and
            (Test-Path -LiteralPath $vertexGlsl -PathType Leaf) -and
            -not $Force) {
            Write-Host "Keeping existing offline permutation: $prefix"
            continue
        }
        & dotnet $ExporterDll `
            --bnsh $Archive `
            --variation $variation `
            --output $prefix
        if ($LASTEXITCODE -ne 0) {
            throw "Shader variation extraction failed: $variation"
        }
        & $ShaderDecoderExe "$prefix.fsh.maxwell" $fragmentGlsl
        if ($LASTEXITCODE -ne 0) {
            throw "Offline fragment shader decompilation failed: $variation"
        }
        & $ShaderDecoderExe "$prefix.vsh.maxwell" $vertexGlsl
        if ($LASTEXITCODE -ne 0) {
            throw "Offline vertex shader decompilation failed: $variation"
        }
    }
}

$ShaderStudyRoot = [IO.Path]::GetFullPath($ShaderStudyRoot)
if (-not (Test-Path -LiteralPath $ShaderStudyRoot -PathType Container)) {
    throw "Shader-study directory is missing: $ShaderStudyRoot"
}
$ExporterDll = Resolve-RequiredFile $ExporterDll 'Trinity exporter'
$ShaderDecoderExe = Resolve-RequiredFile $ShaderDecoderExe 'Maxwell shader decoder'
$sssArchive = Resolve-RequiredFile (
    Join-Path $ShaderStudyRoot 'sss.bnsh') 'SSS shader archive'
$eyeArchive = Resolve-RequiredFile (
    Join-Path $ShaderStudyRoot 'eye_clear_coat.bnsh') 'Eye shader archive'

# Ryujinx.ShaderTools currently targets .NET 8 while this machine may carry a
# newer runtime only. Runtime roll-forward changes no shader input or output.
$env:DOTNET_ROLL_FORWARD = 'Major'

Export-PermutationSet `
    -Archive $sssArchive `
    -OutputDirectory (Join-Path $ShaderStudyRoot (
        'sss-binding-differential-20260812')) `
    -Variations @(0, 8, 40, 48, 56)
Export-PermutationSet `
    -Archive $eyeArchive `
    -OutputDirectory (Join-Path $ShaderStudyRoot (
        'eye-binding-differential-20260812')) `
    -Variations @(0, 20, 44, 52)

Write-Host 'SV Eevee shader permutations extracted and decompiled offline.'
