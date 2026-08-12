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
    @{ Model = '0130_Gyarados_SV_SourceCompare'; File = 'pm0130_00_00_body_a_rgn_RoughnessMap_cf5e4d94dd47.png' },
    @{ Model = '0130_Gyarados_SV_SourceCompare'; File = 'pm0130_00_00_body_b_rgn_RoughnessMap_3d55a141d141.png' },
    @{ Model = '0130_Gyarados_SV_Female_SourceCompare'; File = 'pm0130_00_00_body_a_rgn_RoughnessMap_d54057e80a51.png' },
    @{ Model = '0130_Gyarados_SV_Female_SourceCompare'; File = 'pm0130_00_00_body_b_rgn_RoughnessMap_e6d63a25ae01.png' },
    @{ Model = '0135_Jolteon_SV_SourceCompare'; File = 'pm0135_00_00_body_a_rgn_RoughnessMap_8aa2e91a967a.png' },
    @{ Model = '0135_Jolteon_SV_SourceCompare'; File = 'pm0135_00_00_body_b_rgn_RoughnessMap_10579e16bd76.png' },
    @{ Model = '0136_Flareon_SV_SourceCompare'; File = 'pm0136_00_00_body_a_rgn_RoughnessMap_cf70c9e58c9a.png' },
    @{ Model = '0136_Flareon_SV_SourceCompare'; File = 'pm0136_00_00_body_b_rgn_RoughnessMap_7b05c10f9362.png' },
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
