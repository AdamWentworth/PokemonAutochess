# Subprocess contract tests for tools/run_ctest_ci.ps1.
#
# The wrapper's whole job is to turn a real CTest run into trustworthy evidence,
# so these cases execute the wrapper as a child process against small synthetic
# CTest trees instead of re-implementing it in a text assertion:
#
#   * a clean selection passes
#   * a *passing* test that still prints a render/asset fallback diagnostic fails
#     (this is the false-green the wrapper previously allowed: --output-on-failure
#     never prints passing-test output, so only CTest's LastTest.log carries it)
#   * a selection that registers no tests fails
#   * a genuine CTest failure keeps CTest's own exit code and names the test
#   * a private-content selection that disagrees with the coverage manifest fails
#   * an unreadable coverage manifest fails and still writes the summary
#
# The child shell is the shell that is running this file, so the fixture proves
# the wrapper under both Windows PowerShell 5.1 and PowerShell 7.

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$wrapperPath = Join-Path $repoRoot "tools/run_ctest_ci.ps1"
$shellHost = (Get-Process -Id $PID).Path
$cmakeExe = ((Get-Command cmake -ErrorAction Stop).Source).Replace('\', '/')
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ("pac-ctest-wrapper-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null

$fallbackMarker = "combat tuning fallback engaged"
$checks = 0

function Assert-True {
    param([bool]$Condition, [string]$Message)
    $script:checks += 1
    if (-not $Condition) { throw $Message }
}

function Assert-Match {
    param([string]$Value, [string]$Pattern, [string]$Message)
    $script:checks += 1
    if ($Value -notmatch $Pattern) {
        throw ("{0} (value: {1})" -f $Message, $Value)
    }
}

function New-CtestTree {
    param(
        [string]$Name,
        [AllowEmptyCollection()] [string[]]$Tests = @()
    )

    $dir = Join-Path $fixtureRoot $Name
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    $body = if ($Tests.Count -gt 0) { ($Tests -join "`n") + "`n" } else { "# no tests were registered`n" }
    [IO.File]::WriteAllText((Join-Path $dir "CTestTestfile.cmake"), $body)
    return $dir
}

function New-PassingTestLine {
    param([string]$Name, [string]$Message)
    return ('add_test({0} "{1}" -E echo "{2}")' -f $Name, $cmakeExe, $Message)
}

function New-FailingTestLine {
    param([string]$Name, [int]$ExitCode)
    return ('add_test({0} "{1}" -NoProfile -Command "exit {2}")' -f $Name, $shellHost.Replace('\', '/'), $ExitCode)
}

function New-PrivateTestLine {
    param([string]$Name)
    return ('add_test({0} "{1}" -E echo "private")' -f $Name, $cmakeExe) + "`n" +
        ('set_tests_properties({0} PROPERTIES LABELS "private-content")' -f $Name)
}

function Write-CoverageManifest {
    param(
        [string]$BuildDir,
        [string]$Mode,
        [string]$PrivateTestName = "fixture_private"
    )

    $ciDir = Join-Path $BuildDir "ci"
    New-Item -ItemType Directory -Path $ciDir -Force | Out-Null
    $suite = [pscustomobject][ordered]@{
        name = "fixture-suite"
        status = $(if ($Mode -eq "content") { "enabled" } else { "excluded" })
        prerequisites = @("fixture-prerequisite")
        missing = $(if ($Mode -eq "content") { @() } else { @("fixture-prerequisite") })
        tests = @($PrivateTestName)
    }
    $excluded = @()
    if ($Mode -eq "source") {
        $excluded = @([pscustomobject][ordered]@{
            name = $PrivateTestName
            requires_suites = @("fixture-suite")
            reason = "fixture source scope"
        })
    }
    $manifest = [pscustomobject][ordered]@{
        schema = "pac-ci-coverage-manifest-v1"
        mode = $Mode
        private_content_root = $fixtureRoot
        counts = [pscustomobject][ordered]@{
            registered_contract_tests = $(if ($Mode -eq "content") { 2 } else { 1 })
            private_content_tests = 1
            excluded_private_tests = $(if ($Mode -eq "source") { 1 } else { 0 })
            suites = 1
        }
        suites = @($suite)
        excluded_tests = $excluded
    }
    $path = Join-Path $ciDir "coverage-manifest.json"
    ($manifest | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $path -Encoding UTF8
    return $path
}

# Reads the summary the wrapper is contractually required to write, including for
# failures that happen before CTest is ever invoked.
function Get-WrapperSummary {
    param([string]$BuildDir)

    $path = Join-Path $BuildDir "ci/ctest-summary.json"
    Assert-True (Test-Path -LiteralPath $path -PathType Leaf) ("the wrapper must always write {0}" -f $path)
    return (Get-Content -LiteralPath $path -Raw | ConvertFrom-Json)
}

function Invoke-CtestWrapper {
    param(
        [string]$BuildDir,
        [string]$ExpectedMode,
        [string]$ManifestPath = "",
        [switch]$RejectRuntimeFallback
    )

    $wrapperArguments = @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $wrapperPath,
        "-BuildDir", $BuildDir,
        "-Config", "Debug",
        "-ExpectedMode", $ExpectedMode
    )
    if (-not [string]::IsNullOrWhiteSpace($ManifestPath)) {
        $wrapperArguments += @("-CoverageManifest", $ManifestPath)
    }
    if ($RejectRuntimeFallback) {
        $wrapperArguments += "-RejectRuntimeFallback"
    }

    Push-Location $repoRoot
    try {
        $output = @(& $shellHost @wrapperArguments 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($null -eq $exitCode) { $exitCode = 0 }
    return [pscustomobject][ordered]@{
        exit_code = [int]$exitCode
        output = @($output | ForEach-Object { [string]$_ })
    }
}

try {
    # ---------------- A clean selection passes ----------------
    $cleanBuild = New-CtestTree -Name "clean" -Tests @(
        (New-PassingTestLine -Name "passing" -Message "all good")
    )
    $cleanManifest = Write-CoverageManifest -BuildDir $cleanBuild -Mode "source"
    $clean = Invoke-CtestWrapper -BuildDir $cleanBuild -ExpectedMode "source" -ManifestPath $cleanManifest -RejectRuntimeFallback
    Assert-True ($clean.exit_code -eq 0) ("a clean CTest selection must pass (exit {0}): {1}" -f $clean.exit_code, ($clean.output -join " | "))
    $cleanSummary = Get-WrapperSummary -BuildDir $cleanBuild
    Assert-True ([bool]$cleanSummary.passed) "a clean selection must report passed=true."
    Assert-True ($cleanSummary.scope_mode -eq "source") "the summary must record the configured scope."
    Assert-True ([int]$cleanSummary.registered_tests -ge 1) "the summary must record how many tests were registered."
    Assert-True ([int]$cleanSummary.ctest_exit_code -eq 0) "the summary must record CTest's own exit code."

    # ---------------- A passing test with a fallback diagnostic fails ----------------
    $fallbackBuild = New-CtestTree -Name "fallback" -Tests @(
        (New-PassingTestLine -Name "passing" -Message "all good"),
        (New-PassingTestLine -Name "fallback" -Message $fallbackMarker)
    )
    $fallbackManifest = Write-CoverageManifest -BuildDir $fallbackBuild -Mode "source"
    $fallback = Invoke-CtestWrapper -BuildDir $fallbackBuild -ExpectedMode "source" -ManifestPath $fallbackManifest -RejectRuntimeFallback
    Assert-True ($fallback.exit_code -ne 0) "a passing test that prints a render/asset fallback diagnostic must fail the wrapper."
    $fallbackSummary = Get-WrapperSummary -BuildDir $fallbackBuild
    Assert-True (-not [bool]$fallbackSummary.passed) "the rejected fallback must be recorded as not passed."
    Assert-True (@($fallbackSummary.fallback_failures).Count -ge 1) "the summary must name the rejected fallback diagnostic."
    Assert-Match ((@($fallbackSummary.fallback_failures) -join " | ")) ([regex]::Escape($fallbackMarker)) `
        "the summary must quote the fallback diagnostic it rejected."
    Assert-True ([int]$fallbackSummary.ctest_exit_code -eq 0) "the underlying CTest run still exited zero, so only the diagnostic rejection fails the run."
    $lastTestLog = Join-Path $fallbackBuild "Testing/Temporary/LastTest.log"
    Assert-True (Test-Path -LiteralPath $lastTestLog -PathType Leaf) "the wrapper must leave CTest's own log behind as evidence."
    Assert-Match ((Get-Content -LiteralPath $lastTestLog -Raw)) ([regex]::Escape($fallbackMarker)) `
        "CTest's LastTest.log must still contain the rejected diagnostic."
    Assert-True ([bool]$fallbackSummary.last_test_log_scanned) "the summary must record that CTest's log was scanned."

    # ---------------- A selection with no tests fails ----------------
    $emptyBuild = New-CtestTree -Name "empty"
    $emptyManifest = Write-CoverageManifest -BuildDir $emptyBuild -Mode "source"
    $empty = Invoke-CtestWrapper -BuildDir $emptyBuild -ExpectedMode "source" -ManifestPath $emptyManifest -RejectRuntimeFallback
    Assert-True ($empty.exit_code -ne 0) "a selection that registers no tests must fail."
    $emptySummary = Get-WrapperSummary -BuildDir $emptyBuild
    Assert-Match ([string]$emptySummary.error) "no tests" "the empty selection must be reported as selecting no tests."
    Assert-True (-not [bool]$emptySummary.passed) "an empty selection must not report passed=true."

    # ---------------- A genuine CTest failure keeps its own exit code ----------------
    $failingBuild = New-CtestTree -Name "failing" -Tests @(
        (New-FailingTestLine -Name "failing_case" -ExitCode 7)
    )
    $failingManifest = Write-CoverageManifest -BuildDir $failingBuild -Mode "source"
    # Windows PowerShell turns a native command's stderr into a terminating
    # error under -ErrorActionPreference Stop, so the reference run is scoped the
    # same way the wrapper scopes its own CTest invocation.
    $ErrorActionPreference = "Continue"
    $directCtest = @(& ctest --test-dir $failingBuild -C Debug --no-tests=error 2>&1)
    $directExit = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    Assert-True ($directExit -ne 0) "the fixture must produce a genuine CTest failure."
    $failing = Invoke-CtestWrapper -BuildDir $failingBuild -ExpectedMode "source" -ManifestPath $failingManifest -RejectRuntimeFallback
    Assert-True ($failing.exit_code -eq [int]$directExit) `
        ("the wrapper must preserve CTest's exit code ({0}), not translate it to {1}." -f $directExit, $failing.exit_code)
    $failingSummary = Get-WrapperSummary -BuildDir $failingBuild
    Assert-True (@($failingSummary.failed_tests).Count -ge 1) "the summary must name the failing test."
    Assert-Match ((@($failingSummary.failed_tests) -join " | ")) "failing_case" "the failing test name must be recorded."
    Assert-True (-not [bool]$failingSummary.passed) "a failed CTest run must not report passed=true."

    # ---------------- A mismatched private-content selection fails ----------------
    $mismatchBuild = New-CtestTree -Name "mismatch" -Tests @(
        (New-PassingTestLine -Name "passing" -Message "all good"),
        (New-PrivateTestLine -Name "private_other")
    )
    $mismatchManifest = Write-CoverageManifest -BuildDir $mismatchBuild -Mode "content" -PrivateTestName "fixture_private"
    $mismatch = Invoke-CtestWrapper -BuildDir $mismatchBuild -ExpectedMode "content" -ManifestPath $mismatchManifest
    Assert-True ($mismatch.exit_code -ne 0) "a private-content selection that disagrees with the manifest must fail."
    $mismatchSummary = Get-WrapperSummary -BuildDir $mismatchBuild
    Assert-Match ([string]$mismatchSummary.error) "private-content selection" `
        "the mismatch must be reported as a private-content selection difference."

    # ---------------- A source scope that still registers a private-labelled
    # test fails: the source suite must select zero private-content entries, so a
    # stray label cannot be hidden behind an agreed count.
    $labelledSourceBuild = New-CtestTree -Name "labelled-source" -Tests @(
        (New-PassingTestLine -Name "passing" -Message "all good"),
        (New-PrivateTestLine -Name "fixture_private")
    )
    $labelledSourceManifest = Write-CoverageManifest -BuildDir $labelledSourceBuild -Mode "source"
    $labelledSource = Invoke-CtestWrapper -BuildDir $labelledSourceBuild -ExpectedMode "source" -ManifestPath $labelledSourceManifest
    Assert-True ($labelledSource.exit_code -ne 0) "a source scope that registers a private-content test must fail."
    $labelledSourceSummary = Get-WrapperSummary -BuildDir $labelledSourceBuild
    Assert-Match ([string]$labelledSourceSummary.error) "zero private-content tests" `
        "the stray private-content registration must be named in the summary."

    # ---------------- An unreadable manifest fails with a summary ----------------
    $missingBuild = New-CtestTree -Name "missing-manifest" -Tests @(
        (New-PassingTestLine -Name "passing" -Message "all good")
    )
    $missingManifest = Join-Path $fixtureRoot "absent/coverage-manifest.json"
    $missing = Invoke-CtestWrapper -BuildDir $missingBuild -ExpectedMode "source" -ManifestPath $missingManifest
    Assert-True ($missing.exit_code -ne 0) "an unreadable coverage manifest must fail the wrapper."
    $missingSummary = Get-WrapperSummary -BuildDir $missingBuild
    Assert-Match ([string]$missingSummary.error) "not found" "the unreadable manifest must be named in the summary."
    Assert-True (-not [bool]$missingSummary.passed) "a pre-run failure must not report passed=true."

    Write-Host ("[CiCtestWrapperContractTest] PASS ({0} checks)" -f $checks)
} finally {
    if (Test-Path -LiteralPath $fixtureRoot) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
