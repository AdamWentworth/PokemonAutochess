param(
    [string]$UnpackedRoot = "",
    [string]$ToolboxRoot = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$layers = @(
    [ordered]@{
        Name = "grass02"
        Profile = "route1.grass02.profile.json"
        Cache = "cache\lgpe\route1_grass02"
        Report = "docs\lgpe\evidence\route1_grass02_canonical_scene_report.json"
    },
    [ordered]@{
        Name = "flowers02"
        Profile = "route1.flowers02.profile.json"
        Cache = "cache\lgpe\route1_flowers02"
        Report = "docs\lgpe\evidence\route1_flowers02_canonical_scene_report.json"
    },
    [ordered]@{
        Name = "flowers04"
        Profile = "route1.flowers04.profile.json"
        Cache = "cache\lgpe\route1_flowers04"
        Report = "docs\lgpe\evidence\route1_flowers04_canonical_scene_report.json"
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
        "[LGPEBuildmodelVegetationCook] layer=$($layer.Name) cache=$cacheRoot")
}
