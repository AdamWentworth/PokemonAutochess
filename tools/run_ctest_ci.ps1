param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug",
    [string]$CoverageManifest = "",
    [string]$ExpectedMode = "",
    [switch]$RejectRuntimeFallback
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "ci/CiCoverage.psm1") -Force

if ([string]::IsNullOrWhiteSpace($CoverageManifest)) {
    $CoverageManifest = Join-Path $BuildDir "ci/coverage-manifest.json"
}

$outputDir = Join-Path $BuildDir "ci"
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$ctestLogPath = Join-Path $outputDir "ctest.log"
$summaryPath = Join-Path $outputDir "ctest-summary.json"
$lastTestLogPath = Join-Path $BuildDir "Testing/Temporary/LastTest.log"
$failedTestsLogPath = Join-Path $BuildDir "Testing/Temporary/LastTestsFailed.log"

$summary = [ordered]@{
    schema = "pac-ci-ctest-summary-v1"
    build_dir = $BuildDir
    config = $Config
    scope_mode = $null
    coverage_manifest = $CoverageManifest
    coverage_counts = $null
    registered_tests = 0
    expected_private_content_tests = 0
    ctest_exit_code = $null
    rejected_runtime_fallback = [bool]$RejectRuntimeFallback
    last_test_log_scanned = $false
    fallback_failures = @()
    failed_tests = @()
    error = $null
    passed = $false
    ctest_log = $ctestLogPath
}

function Write-CiCtestSummary {
    $summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

    if ([string]::IsNullOrWhiteSpace($env:GITHUB_STEP_SUMMARY)) { return }
    Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value ("### CTest ({0} scope)" -f $summary.scope_mode)
    Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value ("- registered: {0}" -f $summary.registered_tests)
    Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value ("- private content tests declared: {0}" -f $summary.expected_private_content_tests)
    if ($null -ne $summary.error) {
        Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "#### Failure"
        Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value ("- {0}" -f $summary.error)
    }
    if (@($summary.failed_tests).Count -gt 0) {
        Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "#### Failures"
        foreach ($failedTest in @($summary.failed_tests)) {
            Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "- ``$failedTest``"
        }
    }
    if (@($summary.fallback_failures).Count -gt 0) {
        Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "#### Rejected render or asset fallbacks"
        foreach ($fallbackFailure in @($summary.fallback_failures)) {
            Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "- ``$fallbackFailure``"
        }
    }
}

function Write-CiAnnotation {
    param(
        [string]$Title,
        [string]$Message
    )

    $escaped = ([string]$Message).Replace("%", "%25").Replace("`r", "%0D").Replace("`n", "%0A")
    Write-Host ("::error title={0}::{1}" -f $Title, $escaped)
}

# A wrapper that cannot even state what it selected is not evidence, so the
# pre-run failures still write the machine-readable summary before exiting.
try {
    $manifest = Read-CiCoverageManifest -Path $CoverageManifest
    $summary.coverage_counts = $manifest.counts
    if ($null -ne $manifest.counts) {
        $summary.expected_private_content_tests = [int](Get-CiPropertyValue $manifest.counts 'private_content_tests')
    }
    $scopeMode = Assert-CiCtestScope -BuildDir $BuildDir -Config $Config -Manifest $manifest -ExpectedMode $ExpectedMode
    $summary.scope_mode = $scopeMode

    $registeredCount = Get-CtestRegisteredTestCount -BuildDir $BuildDir -Config $Config
    $summary.registered_tests = $registeredCount
    if ($registeredCount -le 0) {
        throw ("CTest registered no tests for '{0}' ({1}). A selection that runs nothing is a failure." -f $BuildDir, $Config)
    }
} catch {
    $summary.error = $_.Exception.Message
    $summary.ctest_exit_code = 1
    $summary.passed = $false
    Write-CiAnnotation -Title "CTest pre-run failure" -Message $summary.error
    Write-CiCtestSummary
    Write-Host ("[CtestCi] FAIL: {0}" -f $summary.error)
    exit 1
}

foreach ($line in @(Get-CiCoverageSummaryLines -Manifest $manifest)) {
    Write-Host $line
}
Write-Host ("[CtestCi] Executing {0} registered CTest entries for scope '{1}'." -f $registeredCount, $scopeMode)

# --output-on-failure only prints failing tests, so a passing test that still
# emits a render or asset fallback diagnostic leaves its evidence in CTest's
# LastTest.log. Any stale log is removed first so a marker from an earlier run
# can never be attributed to this one. The whole post-selection body is guarded
# so an unexpected error still writes the machine-readable summary instead of
# leaving the operator with no evidence at all.
try {
$ctestStartedUtc = [DateTime]::UtcNow
if (Test-Path -LiteralPath $lastTestLogPath) {
    Remove-Item -LiteralPath $lastTestLogPath -Force
}

$ErrorActionPreference = "Continue"
$ctestOutput = @(& ctest --test-dir $BuildDir -C $Config --output-on-failure --no-tests=error 2>&1)
$ctestExitCode = $LASTEXITCODE
$ErrorActionPreference = "Stop"
if ($null -eq $ctestExitCode) { $ctestExitCode = 0 }

$ctestLines = @($ctestOutput | ForEach-Object { [string]$_ })
$ctestLines | Set-Content -LiteralPath $ctestLogPath -Encoding UTF8
$ctestLines | ForEach-Object { Write-Host $_ }

$scannedOutput = New-Object 'System.Collections.Generic.List[string]'
foreach ($line in $ctestLines) { $scannedOutput.Add($line) }
if (Test-Path -LiteralPath $lastTestLogPath -PathType Leaf) {
    $lastTestLogItem = Get-Item -LiteralPath $lastTestLogPath
    if ($lastTestLogItem.LastWriteTimeUtc -ge $ctestStartedUtc) {
        $summary.last_test_log_scanned = $true
        foreach ($line in @(Get-Content -LiteralPath $lastTestLogPath)) {
            $scannedOutput.Add([string]$line)
        }
    }
} else {
    Write-Host ("[CtestCi] No CTest log was produced at {0}; scanning the console transcript only." -f $lastTestLogPath)
}

$fallbackFailures = @()
if ($RejectRuntimeFallback) {
    if (-not $summary.last_test_log_scanned) {
        throw "Runtime fallback checking requires the complete fresh CTest LastTest.log; console output omits passing-test diagnostics."
    }
    $fallbackFailures = @(Get-CiRuntimeFallbackMarker -Lines $scannedOutput.ToArray())
}
$summary.fallback_failures = @($fallbackFailures)

$failedTests = @()
if ($ctestExitCode -ne 0) {
    if (Test-Path -LiteralPath $failedTestsLogPath) {
        # Cast to string: Windows PowerShell decorates Get-Content lines with
        # PSPath/PSDrive note properties, and those decorated objects serialise
        # into the summary as nested records instead of plain test names.
        $failedTests = @(Get-Content -LiteralPath $failedTestsLogPath |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            ForEach-Object { [string]$_ })
    }
    if ($failedTests.Count -eq 0) {
        $failedTests = @("CTest exited with code $ctestExitCode; LastTestsFailed.log was unavailable.")
    }
    foreach ($failedTest in $failedTests) {
        Write-CiAnnotation -Title "CTest failure" -Message $failedTest
    }
}
$summary.failed_tests = @($failedTests)

if ($fallbackFailures.Count -gt 0) {
    foreach ($fallbackFailure in $fallbackFailures) {
        Write-CiAnnotation -Title "Render or asset fallback" -Message $fallbackFailure
    }
}

$summary.ctest_exit_code = $ctestExitCode
$summary.passed = ($ctestExitCode -eq 0 -and $fallbackFailures.Count -eq 0)
Write-CiCtestSummary

if ($ctestExitCode -ne 0) {
    Write-Host ("[CtestCi] FAIL: CTest exited with code {0}." -f $ctestExitCode)
    exit $ctestExitCode
}
if ($fallbackFailures.Count -gt 0) {
    Write-Host ("[CtestCi] FAIL: {0} render or asset fallback diagnostics were reported." -f $fallbackFailures.Count)
    exit 1
}

Write-Host "[CtestCi] PASS"
exit 0
} catch {
    $summary.error = $_.Exception.Message
    $summary.ctest_exit_code = 1
    $summary.passed = $false
    Write-CiAnnotation -Title "CTest wrapper failure" -Message $summary.error
    Write-CiCtestSummary
    Write-Host ("[CtestCi] FAIL: {0}" -f $summary.error)
    exit 1
}
