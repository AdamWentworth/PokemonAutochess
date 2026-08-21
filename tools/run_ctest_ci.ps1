param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ErrorActionPreference = "Continue"
& ctest --test-dir $BuildDir -C $Config --output-on-failure 2>&1 |
    ForEach-Object { Write-Host $_ }
$ctestExitCode = $LASTEXITCODE
$ErrorActionPreference = "Stop"

if ($ctestExitCode -eq 0) {
    Write-Host "[CtestCi] PASS"
    exit 0
}

$failedLog = Join-Path $BuildDir "Testing\Temporary\LastTestsFailed.log"
$failedTests = @()
if (Test-Path -LiteralPath $failedLog) {
    $failedTests = @(Get-Content -LiteralPath $failedLog |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}
if ($failedTests.Count -eq 0) {
    $failedTests = @("CTest exited with code $ctestExitCode; LastTestsFailed.log was unavailable.")
}

foreach ($failedTest in $failedTests) {
    $escaped = $failedTest.Replace("%", "%25").Replace("`r", "%0D").Replace("`n", "%0A")
    Write-Host "::error title=CTest failure::$escaped"
}

if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_STEP_SUMMARY)) {
    Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "### CTest failures"
    foreach ($failedTest in $failedTests) {
        Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "- ``$failedTest``"
    }
}

exit $ctestExitCode
