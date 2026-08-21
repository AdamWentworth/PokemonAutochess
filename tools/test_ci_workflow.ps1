param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-Condition {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$workflowPath = Join-Path $repoRoot ".github\workflows\ci.yml"
$manifestPath = Join-Path $repoRoot "vcpkg.json"
$smokePath = Join-Path $repoRoot "tools\runtime_visual_smoke.ps1"

$workflow = Get-Content -LiteralPath $workflowPath -Raw
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$smoke = Get-Content -LiteralPath $smokePath -Raw

Assert-Condition ($workflow -match '(?m)^permissions:\r?\n  contents: read\s*$') `
    "CI must use an explicit read-only default token permission."
Assert-Condition ($workflow -match '(?m)^concurrency:\s*$') `
    "CI must cancel superseded branch runs."

$mutableOfficialActions = [regex]::Matches(
    $workflow,
    'uses:\s+actions/(checkout|cache|upload-artifact)@v\d+')
Assert-Condition ($mutableOfficialActions.Count -eq 0) `
    "Official GitHub Actions must be pinned to immutable commit SHAs."

$pinnedOfficialActions = [regex]::Matches(
    $workflow,
    'uses:\s+actions/(checkout|cache|upload-artifact)@[0-9a-f]{40}\s+#\s+v\d+\.\d+\.\d+')
Assert-Condition ($pinnedOfficialActions.Count -eq 5) `
    "Expected two checkout, two cache, and one upload-artifact immutable pins."

$baseline = [string]$manifest.'builtin-baseline'
Assert-Condition (-not [string]::IsNullOrWhiteSpace($baseline)) `
    "vcpkg.json must declare a builtin baseline."
Assert-Condition ($workflow -match "(?m)^\s+VCPKG_COMMIT:\s+$([regex]::Escape($baseline))\s*$") `
    "CI's vcpkg executable commit must match the manifest builtin baseline."
Assert-Condition ($workflow -notmatch 'vcpkg.*fetch\s+--depth') `
    "vcpkg must retain registry history so manifest version overrides remain resolvable."

$standaloneConfigureCount = [regex]::Matches(
    $workflow,
    '-DPAC_BUILD_EDITOR=OFF').Count
Assert-Condition ($standaloneConfigureCount -eq 2) `
    "Both hosted-runner configure commands must disable the local-only editor package graph."
Assert-Condition ($workflow -match 'FORMAT_BASE_REF:.*github\.event\.before') `
    "Push format checks must compare against the pushed event's previous revision."
Assert-Condition ($workflow -match 'powershell .*tools/check_script_syntax\.ps1') `
    "CI must validate the tracked PowerShell and Python syntax before configuring C++."

Assert-Condition ($smoke -match 'Set-SmokeEnvVar\s+-Name\s+"PAC_AUTO_QUIT_FRAMES"') `
    "Runtime visual smoke must have a frame-bounded exit policy."
Assert-Condition ($smoke -match '\$ScreenshotFrame\s*\+\s*2') `
    "Runtime visual smoke must exit shortly after its requested capture frame."

Write-Host "[CiWorkflowContractTest] PASS"
