param(
    [string]$UnpackedRoot = "",
    [string]$ToolboxRoot = "",
    [string]$OutputRoot = "",
    [string]$ReportPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot "cache\lgpe\route1"
}
if ([string]::IsNullOrWhiteSpace($ReportPath)) {
    $ReportPath = Join-Path $repoRoot (
        "docs\lgpe\evidence\route1_canonical_scene_report.json")
}

$arguments = @{
    ProfilePath = Join-Path $PSScriptRoot "route1.profile.json"
    OutputPath = Join-Path $OutputRoot "source_manifest.json"
    CanonicalOutputRoot = $OutputRoot
    CanonicalReportPath = $ReportPath
}
if (-not [string]::IsNullOrWhiteSpace($UnpackedRoot)) {
    $arguments.UnpackedRoot = $UnpackedRoot
}
if (-not [string]::IsNullOrWhiteSpace($ToolboxRoot)) {
    $arguments.ToolboxRoot = $ToolboxRoot
}

& (Join-Path $PSScriptRoot "export_lgpe_source_manifest.ps1") @arguments
