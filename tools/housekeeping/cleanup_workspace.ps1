[CmdletBinding()]
param(
    [string]$GameRoot = "",
    [string]$EngineRoot = "",
    [ValidateSet('AllRegenerable', 'HistoricalBuilds', 'Caches')]
    [string]$Scope = 'AllRegenerable',
    [string]$OutputDirectory = "",
    [switch]$Execute,
    [switch]$ConfirmDeletion
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $scriptRoot '..\..'
}
$GameRoot = [IO.Path]::GetFullPath($GameRoot)

if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
    $EngineRoot = Join-Path $GameRoot '..\..\Phlosion\PhlosionEngine'
}
$EngineRoot = [IO.Path]::GetFullPath($EngineRoot)

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmssZ')
    $OutputDirectory = Join-Path $GameRoot "artifacts\housekeeping\cleanup-$stamp"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)

Import-Module (Join-Path $scriptRoot 'HousekeepingCleanup.psm1') -Force

$plan = New-WorkspaceCleanupPlan `
    -GameRoot $GameRoot `
    -EngineRoot $EngineRoot `
    -Scope $Scope
$plan.execution_requested = [bool]$Execute
$plan.confirmation_provided = [bool]$ConfirmDeletion
Assert-WorkspaceCleanupPlan $plan

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$jsonPath = Join-Path $OutputDirectory 'cleanup_plan.json'
$markdownPath = Join-Path $OutputDirectory 'cleanup_plan.md'
$plan | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

$builder = [Text.StringBuilder]::new()
[void]$builder.AppendLine('# Workspace Cleanup Plan')
[void]$builder.AppendLine()
[void]$builder.AppendLine("Generated: $($plan.generated_at_utc)")
[void]$builder.AppendLine()
if ($Execute) {
    [void]$builder.AppendLine('This plan accompanies an explicit execution request. It excludes active builds, artifacts, assets, content, source, configuration, tests, and documentation.')
} else {
    [void]$builder.AppendLine('This plan is dry-run output. It excludes active builds, artifacts, assets, content, source, configuration, tests, and documentation.')
}
[void]$builder.AppendLine()
[void]$builder.AppendLine("- Scope: $($plan.scope)")
[void]$builder.AppendLine("- Existing targets: $($plan.existing_target_count) / $($plan.target_count)")
[void]$builder.AppendLine("- Files: $($plan.reclaimable_files)")
[void]$builder.AppendLine("- Directories: $($plan.reclaimable_directories)")
[void]$builder.AppendLine("- Bytes: $($plan.reclaimable_bytes)")
[void]$builder.AppendLine()
[void]$builder.AppendLine('| Owner | Category | Target | Exists | Files | Directories | Bytes |')
[void]$builder.AppendLine('| --- | --- | --- | --- | ---: | ---: | ---: |')
foreach ($target in @($plan.targets)) {
    [void]$builder.AppendLine("| $($target.owner) | $($target.category) | ``$($target.target)`` | $($target.exists) | $($target.file_count) | $($target.directory_count) | $($target.bytes) |")
}
[void]$builder.AppendLine()
[void]$builder.AppendLine('Deletion requires both `-Execute` and `-ConfirmDeletion`. The plan is revalidated immediately before removal and is rejected if any target changed or contains a reparse point.')
Set-Content -LiteralPath $markdownPath -Value $builder.ToString() -Encoding UTF8

Write-Host "Cleanup plan: $markdownPath"
Write-Host "Machine record: $jsonPath"
Write-Host "Reclaimable: $($plan.reclaimable_bytes) bytes across $($plan.existing_target_count) existing targets."

$result = $null
$resultPath = $null
if ($Execute) {
    $result = Invoke-WorkspaceCleanupPlan `
        -Plan $plan `
        -ConfirmDeletion:$ConfirmDeletion
    $resultPath = Join-Path $OutputDirectory 'cleanup_result.json'
    $result | ConvertTo-Json -Depth 6 | Set-Content `
        -LiteralPath $resultPath `
        -Encoding UTF8
    Write-Host "Removed $($result.removed_bytes) bytes from $($result.removed_target_count) targets."
} else {
    Write-Host 'Dry run only; no files or directories were removed.'
}

[pscustomobject]@{
    PlanPath = $jsonPath
    ReportPath = $markdownPath
    ResultPath = $resultPath
    Plan = $plan
    Result = $result
}
