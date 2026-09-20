# Orchestration contract for tools/full_check.ps1 scope handling.
#
# The false green this guards against: full_check applied
# -DPAC_ENABLE_PRIVATE_ASSET_TESTS only when the build cache was absent, so a
# build directory first configured for one scope silently answered for the
# other, and the source scope still ran the private promotion validator. These
# cases drive the real entrypoint through a real CMake configure against a
# scratch cache and a deliberately empty private content root, so the source
# scope proves it needs no private corpus while the content scope fails
# without it.
#
# This fixture configures a real build tree (roughly a minute), so it is a local
# entrypoint contract rather than a hosted-CI step. Run it from either Windows
# PowerShell 5.1 or PowerShell 7:
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_full_check_scope.ps1

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$fullCheck = Join-Path $repoRoot "tools/full_check.ps1"
$shellHost = (Get-Process -Id $PID).Path
$fixtureStem = Join-Path $repoRoot "debug/ci_qualification"
$fixtureRoot = Join-Path $fixtureStem ("full-check-scope-" + [Guid]::NewGuid().ToString("N").Substring(0, 8))
$buildDir = Join-Path $fixtureRoot "build"
$negativeBuildDir = Join-Path $fixtureRoot "build-missing-assets"
$emptyContentRoot = Join-Path $fixtureRoot "empty-content"

if ([string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
    throw "VCPKG_ROOT must be set: the scope contract drives a real CMake configure."
}
# Best-effort sweep of earlier runs: a lingering build tree can still be
# locked by a finishing MSBuild or antivirus scan, which must not fail a fresh
# run, so each run also gets its own directory name.
Get-ChildItem -LiteralPath $fixtureStem -Directory -Filter 'full-check-scope-*' -ErrorAction SilentlyContinue |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Path $emptyContentRoot -Force | Out-Null

$checks = 0
function Assert-True {
    param([bool]$Condition, [string]$Message)
    $script:checks += 1
    if (-not $Condition) { throw $Message }
}
function Assert-Equal {
    param($Expected, $Actual, [string]$Message)
    $script:checks += 1
    if ("$Expected" -ne "$Actual") {
        throw ("{0} (expected '{1}', got '{2}')" -f $Message, $Expected, $Actual)
    }
}

function Invoke-FullCheck {
    param(
        [string]$Name,
        [string[]]$Arguments
    )

    $logPath = Join-Path $fixtureRoot ("{0}.log" -f $Name)
    # Windows PowerShell turns a native command's stderr into a terminating
    # error under -ErrorActionPreference Stop, and CMake is allowed to warn on
    # stderr, so the child is invoked the way the entrypoint invokes its own
    # native tools and the exit code is the only verdict.
    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    Push-Location $repoRoot
    try {
        $output = @(& $shellHost -NoProfile -ExecutionPolicy Bypass -File $fullCheck @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
        $ErrorActionPreference = $previousErrorAction
    }
    if ($null -eq $exitCode) { $exitCode = 0 }
    @($output | ForEach-Object { [string]$_ }) | Set-Content -LiteralPath $logPath -Encoding UTF8
    return [pscustomobject][ordered]@{
        exit_code = [int]$exitCode
        log = $logPath
    }
}

function Read-FullCheckReport {
    param([string]$Path)
    Assert-True (Test-Path -LiteralPath $Path -PathType Leaf) ("the entrypoint must always write {0}" -f $Path)
    return (Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json)
}

function Get-StepStatus {
    param($Report, [string]$Name)
    foreach ($step in @($Report.steps)) {
        if ([string]$step.name -eq $Name) { return [string]$step.status }
    }
    return $null
}

# Reads the requested scope out of the cache the way the build system sees it,
# not out of the report the entrypoint wrote.
function Get-CacheScope {
    param([string]$BuildDirPath)
    $cachePath = Join-Path $BuildDirPath "CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) { return $null }
    $pattern = '^PAC_ENABLE_PRIVATE_ASSET_TESTS:[^=]*=(.*)'
    $match = Select-String -LiteralPath $cachePath -Pattern $pattern | Select-Object -First 1
    if ($null -eq $match) { return $null }
    return [string]$match.Matches[0].Groups[1].Value
}

try {
    # ---------------- Source scope on a fresh cache needs no private corpus ----
    $freshReportPath = Join-Path $fixtureRoot "report-source-fresh.json"
    $fresh = Invoke-FullCheck -Name "source-fresh" -Arguments @(
        "-BuildDir", $buildDir,
        "-SourceScope",
        "-PrivateContentRoot", $emptyContentRoot,
        "-ConfigureOnly",
        "-ReportPath", $freshReportPath)
    Assert-Equal 0 $fresh.exit_code ("the source scope must configure without the private corpus; see {0}" -f $fresh.log)
    $freshReport = Read-FullCheckReport -Path $freshReportPath
    Assert-Equal "passed" $freshReport.status "a fresh source configure must pass"
    Assert-Equal "source" $freshReport.scope "the report must state the requested source scope"
    Assert-Equal "OFF" $freshReport.private_asset_tests "the source scope must configure the private suite OFF"
    Assert-Equal "OFF" (Get-CacheScope $buildDir) "the requested scope must be written into the cache"
    Assert-Equal $false $freshReport.had_cache "the first configure must start without a cache"
    Assert-Equal "ran" (Get-StepStatus $freshReport "configure") "the source scope must configure the build"
    Assert-Equal "skipped" (Get-StepStatus $freshReport "kanto-model-promotions") "the source scope must not run the private promotion validator"

    # ---------------- Existing source cache must answer for the content scope --
    $contentReportPath = Join-Path $fixtureRoot "report-content-existing.json"
    $content = Invoke-FullCheck -Name "content-existing-cache" -Arguments @(
        "-BuildDir", $buildDir,
        "-PrivateContentRoot", $repoRoot,
        "-ConfigureOnly",
        "-ReportPath", $contentReportPath)
    Assert-Equal 0 $content.exit_code ("the content scope must reconfigure the existing source cache; see {0}" -f $content.log)
    $contentReport = Read-FullCheckReport -Path $contentReportPath
    Assert-Equal "content" $contentReport.scope "the report must state the requested content scope"
    Assert-Equal "ON" $contentReport.private_asset_tests "the content scope must configure the private suite ON"
    Assert-Equal "ON" (Get-CacheScope $buildDir) "an existing source cache must be reconfigured to content"
    Assert-Equal $true $contentReport.had_cache "the second configure must reuse the existing cache"
    Assert-Equal "ran" (Get-StepStatus $contentReport "kanto-model-promotions") "the content scope must run the private promotion validator"

    # ---------------- Existing content cache must answer for the source scope --
    $backReportPath = Join-Path $fixtureRoot "report-source-existing.json"
    $back = Invoke-FullCheck -Name "source-existing-cache" -Arguments @(
        "-BuildDir", $buildDir,
        "-SourceScope",
        "-PrivateContentRoot", $emptyContentRoot,
        "-ConfigureOnly",
        "-ReportPath", $backReportPath)
    Assert-Equal 0 $back.exit_code ("the source scope must reconfigure the existing content cache; see {0}" -f $back.log)
    $backReport = Read-FullCheckReport -Path $backReportPath
    Assert-Equal "OFF" (Get-CacheScope $buildDir) "an existing content cache must be reconfigured back to source"
    Assert-Equal "source" $backReport.scope "the report must state the requested source scope"
    Assert-Equal "skipped" (Get-StepStatus $backReport "kanto-model-promotions") "a source run must never run the private promotion validator"

    # ---------------- Content scope with missing assets fails, not shrinks -----
    $negativeReportPath = Join-Path $fixtureRoot "report-content-missing.json"
    $negative = Invoke-FullCheck -Name "content-missing-assets" -Arguments @(
        "-BuildDir", $negativeBuildDir,
        "-PrivateContentRoot", $emptyContentRoot,
        "-ConfigureOnly",
        "-ReportPath", $negativeReportPath)
    Assert-True ($negative.exit_code -ne 0) ("content scope with a missing corpus must fail; see {0}" -f $negative.log)
    $negativeReport = Read-FullCheckReport -Path $negativeReportPath
    Assert-Equal "failed" $negativeReport.status "a missing corpus must not be reported as a pass"
    Assert-Equal $false $negativeReport.passed "the negative run must not claim passed"
    Assert-True ((Get-StepStatus $negativeReport "kanto-model-promotions") -ne "ran") "a configure that fails on missing content must not then run private validators"
    Assert-Equal "failed" (Get-StepStatus $negativeReport "failure") "the failure must be recorded in the report"

    Write-Host ("[FullCheckScopeContractTest] PASS ({0} checks)" -f $checks)
} finally {
    try {
        if (Test-Path -LiteralPath $fixtureRoot) {
            Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    } catch {
        Write-Host ("[FullCheckScopeContractTest] Left the scratch build tree for inspection: {0}" -f $fixtureRoot)
    }
}
