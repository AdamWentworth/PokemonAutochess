# Entrypoint contract for tools/qualify_content.ps1.
#
# The correction cycle found that the qualifier could be driven into lanes that
# were never actually validated: the GPU preflight was only wired into the old
# smoke script, option mistakes were discovered after long build steps, and an
# incomplete lane could still look like a green run. These cases drive the real
# entrypoint as a child process (no CMake and no GPU required) and assert on the
# machine-readable report it promises to keep on failure:
#
#   * a software-only adapter is reported as unqualified before any long step
#   * -SyncDepot without -DepotRoot, and an unsupported editor configuration,
#     fail during option validation
#   * a synthetic hardware adapter cannot qualify the actual machine
#
# The child shell is the shell running this file, so the fixture proves the
# entrypoint under both Windows PowerShell 5.1 and PowerShell 7:
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_qualify_content_entrypoint.ps1

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$entrypoint = Join-Path $repoRoot "tools/qualify_content.ps1"
$shellHost = (Get-Process -Id $PID).Path
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ("pac-qualify-entrypoint-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null

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

function Get-StepNames {
    param($Report)
    return @($Report.steps | ForEach-Object { [string]$_.name })
}

function Get-StepStatus {
    param($Report, [string]$Name)
    foreach ($step in @($Report.steps)) {
        if ([string]$step.name -eq $Name) { return [string]$step.status }
    }
    return $null
}

function Invoke-QualifyContent {
    param(
        [string]$Name,
        [string[]]$Arguments,
        [switch]$WithoutVcpkgRoot
    )

    $logPath = Join-Path $fixtureRoot ("{0}.log" -f $Name)
    $previousErrorAction = $ErrorActionPreference
    $previousVcpkgRoot = $env:VCPKG_ROOT
    # Windows PowerShell turns a native command's stderr into a terminating error
    # under -ErrorActionPreference Stop, so the child's exit code is the verdict.
    $ErrorActionPreference = "Continue"
    if ($WithoutVcpkgRoot) {
        # Pin the fixture to a fast, deterministic stop after the GPU gate: the
        # toolchain check is the first step after the content preflight, and a
        # configured toolchain would otherwise start a real CMake configure.
        $env:VCPKG_ROOT = $null
    }
    Push-Location $repoRoot
    try {
        $output = @(& $shellHost -NoProfile -ExecutionPolicy Bypass -File $entrypoint @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
        $ErrorActionPreference = $previousErrorAction
        $env:VCPKG_ROOT = $previousVcpkgRoot
    }
    if ($null -eq $exitCode) { $exitCode = 0 }
    @($output | ForEach-Object { [string]$_ }) | Set-Content -LiteralPath $logPath -Encoding UTF8
    return [pscustomobject][ordered]@{
        exit_code = [int]$exitCode
        log = $logPath
    }
}

function Read-QualificationReport {
    param([string]$OutputDir, [string]$Log)

    $reportPath = Join-Path $OutputDir "content-qualification-report.json"
    Assert-True (Test-Path -LiteralPath $reportPath -PathType Leaf) `
        ("the entrypoint must keep its report at {0}; see {1}" -f $reportPath, $Log)
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    Assert-Equal "pac-content-qualification-report-v1" $report.schema "the report must declare its schema"
    Assert-True (-not [string]::IsNullOrWhiteSpace([string]$report.game.git_revision)) `
        "the report must record the game revision it ran against"
    Assert-True ($null -ne $report.game.dirty_state) "the report must record the dirty code state"
    return $report
}

try {
    # ---------------- A software-only adapter never qualifies ----------------
    $softwareOut = Join-Path $fixtureRoot "out-software"
    $software = Invoke-QualifyContent -Name "software-adapter" -Arguments @(
        "-BuildDir", (Join-Path $fixtureRoot "build-software"),
        "-OutputDir", $softwareOut,
        "-IncludeVisual",
        "-HardwareAdapter", "Microsoft Basic Render Driver")
    Assert-True ($software.exit_code -ne 0) `
        ("a software-only adapter must not qualify; see {0}" -f $software.log)
    $softwareReport = Read-QualificationReport -OutputDir $softwareOut -Log $software.log
    Assert-Equal "unqualified" $softwareReport.status "a software-only adapter must report unqualified"
    Assert-Equal "Unqualified" (Get-StepStatus $softwareReport "gpu-preflight") `
        "the GPU gate must record the software-only adapter as unqualified"
    Assert-True ((Get-StepNames $softwareReport) -notcontains "configure") `
        "the GPU gate must reject a software adapter before any build step"
    Assert-True ((Get-StepNames $softwareReport) -notcontains "visual-qualification") `
        "the render matrix must not be launched for an unqualified adapter"

    # An override is useful for diagnostic fixtures but is not hardware evidence.
    $hardwareOut = Join-Path $fixtureRoot "out-hardware"
    $hardware = Invoke-QualifyContent -Name "hardware-adapter" -WithoutVcpkgRoot -Arguments @(
        "-BuildDir", (Join-Path $fixtureRoot "build-hardware"),
        "-OutputDir", $hardwareOut,
        "-IncludeVisual",
        "-HardwareAdapter", "NVIDIA GeForce GTX 1070")
    Assert-True ($hardware.exit_code -ne 0) `
        ("an incomplete lane must never be reported as qualified; see {0}" -f $hardware.log)
    $hardwareReport = Read-QualificationReport -OutputDir $hardwareOut -Log $hardware.log
    Assert-Equal "unqualified" $hardwareReport.status "an incomplete lane must report unqualified"
    Assert-Equal "Unqualified" (Get-StepStatus $hardwareReport "gpu-preflight") `
        "a synthetic adapter must not qualify the actual machine"
    Assert-True ((Get-StepNames $hardwareReport) -notcontains "visual-qualification") `
        "the render matrix must not run when the rest of the lane did not qualify"

    # ---------------- Option validation happens before the long steps ----------------
    $syncOut = Join-Path $fixtureRoot "out-sync"
    $sync = Invoke-QualifyContent -Name "sync-without-depot" -Arguments @(
        "-BuildDir", (Join-Path $fixtureRoot "build-sync"),
        "-OutputDir", $syncOut,
        "-SyncDepot")
    Assert-True ($sync.exit_code -ne 0) `
        ("-SyncDepot without -DepotRoot must fail; see {0}" -f $sync.log)
    $syncReport = Read-QualificationReport -OutputDir $syncOut -Log $sync.log
    Assert-Equal "Failed" (Get-StepStatus $syncReport "validate-options") `
        "the missing depot root must be reported as an option failure"
    Assert-True ((Get-StepNames $syncReport) -notcontains "configure") `
        "option validation must finish before any long step"

    $editorOut = Join-Path $fixtureRoot "out-editor"
    $editor = Invoke-QualifyContent -Name "editor-unsupported-config" -Arguments @(
        "-BuildDir", (Join-Path $fixtureRoot "build-editor"),
        "-OutputDir", $editorOut,
        "-IncludeEditor",
        "-Config", "FastTest")
    Assert-True ($editor.exit_code -ne 0) `
        ("an unsupported editor configuration must fail; see {0}" -f $editor.log)
    $editorReport = Read-QualificationReport -OutputDir $editorOut -Log $editor.log
    Assert-Equal "unqualified" $editorReport.status "an unsupported editor configuration must not qualify"
    Assert-Equal "Failed" (Get-StepStatus $editorReport "validate-options") `
        "the unsupported editor configuration must be reported as an option failure"
    Assert-True ((Get-StepNames $editorReport) -notcontains "editor-qualification") `
        "the editor workflow must not run for an unsupported configuration"

    Write-Host ("[QualifyContentEntrypointContractTest] PASS ({0} checks)" -f $checks)
} finally {
    if (Test-Path -LiteralPath $fixtureRoot) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
