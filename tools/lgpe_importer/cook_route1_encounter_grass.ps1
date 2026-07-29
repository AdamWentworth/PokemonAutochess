param(
    [string]$UnpackedRoot = "",
    [string]$ToolboxRoot = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$layers = @(
    [ordered]@{
        Name = "enc_grass01"
        Profile = "route1.enc_grass01.profile.json"
        Cache = "cache\lgpe\route1_enc_grass01"
        Report = "docs\lgpe\evidence\route1_enc_grass01_canonical_scene_report.json"
    },
    [ordered]@{
        Name = "enc_grass02"
        Profile = "route1.enc_grass02.profile.json"
        Cache = "cache\lgpe\route1_enc_grass02"
        Report = "docs\lgpe\evidence\route1_enc_grass02_canonical_scene_report.json"
    }
)

foreach ($layer in $layers) {
    $cacheRoot = Join-Path $repoRoot $layer.Cache
    $arguments = @{
        ProfilePath = Join-Path $PSScriptRoot $layer.Profile
        OutputPath = Join-Path $cacheRoot "source_manifest.json"
        CanonicalOutputRoot = $cacheRoot
        CanonicalReportPath = Join-Path $repoRoot $layer.Report
    }
    if (-not [string]::IsNullOrWhiteSpace($UnpackedRoot)) {
        $arguments.UnpackedRoot = $UnpackedRoot
    }
    if (-not [string]::IsNullOrWhiteSpace($ToolboxRoot)) {
        $arguments.ToolboxRoot = $ToolboxRoot
    }

    & (Join-Path $PSScriptRoot "export_lgpe_source_manifest.ps1") @arguments
    Write-Host (
        "[LGPEEncounterGrassCook] layer=$($layer.Name) cache=$cacheRoot")
}
