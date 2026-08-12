[CmdletBinding()]
param(
    [string]$ScarletImportRoot =
        'D:\ProjectData\Games\PokemonAutochess\Assets\pokemon-autochess\derived\imports\gamefreak\pokemon-scarlet-v3.0.1',
    [string]$GameRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
}

$destinationRoot = Join-Path $GameRoot 'assets\models\za_sv_surface_maps'
[IO.Directory]::CreateDirectory($destinationRoot) | Out-Null

$imports = @(
    @{ Model = '0133_Eevee_SV_SourceCompare'; File = 'pm0133_00_00_body_rgn_RoughnessMap_e66d6d7d0ac0.png' },
    @{ Model = '0130_Gyarados_SV_SourceCompare'; File = 'pm0130_00_00_body_a_rgn_RoughnessMap_cf5e4d94dd47.png' },
    @{ Model = '0130_Gyarados_SV_SourceCompare'; File = 'pm0130_00_00_body_b_rgn_RoughnessMap_3d55a141d141.png' },
    @{ Model = '0130_Gyarados_SV_Female_SourceCompare'; File = 'pm0130_00_00_body_a_rgn_RoughnessMap_d54057e80a51.png' },
    @{ Model = '0130_Gyarados_SV_Female_SourceCompare'; File = 'pm0130_00_00_body_b_rgn_RoughnessMap_e6d63a25ae01.png' },
    @{ Model = '0137_Porygon_SV_SourceCompare'; File = 'pm0137_00_00_body_rgn_RoughnessMap_93db797d858c.png' }
)

foreach ($entry in $imports) {
    $source = Join-Path $ScarletImportRoot (
        '{0}_native-ir\{0}_textures\{1}' -f $entry.Model, $entry.File)
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Missing Scarlet/Violet surface source: $source"
    }
    Copy-Item -LiteralPath $source -Destination (
        Join-Path $destinationRoot $entry.File) -Force
}

Write-Host (
    'Staged {0} Scarlet/Violet roughness maps for Z-A-compatible UV layouts.' -f
        $imports.Count)
