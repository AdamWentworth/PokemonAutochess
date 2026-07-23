param()

$ErrorActionPreference = "Stop"

Import-Module (Join-Path $PSScriptRoot "RendererQualificationReport.psm1") -Force
Import-Module (Join-Path $PSScriptRoot "RendererQualificationEvidence.psm1") -Force

function Assert-Condition {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

$started = [DateTime]::SpecifyKind(
    [DateTime]::Parse("2026-07-22T12:00:00"),
    [DateTimeKind]::Utc)
$finished = $started.AddSeconds(2)
$passedStep = New-RendererQualificationStepResult `
    -Name "native-visual-parity" `
    -Status "Passed" `
    -StartedAtUtc $started `
    -FinishedAtUtc $finished `
    -Artifacts ([pscustomobject]@{ ReportPath = "native/matrix-report.json" })
$failedStep = New-RendererQualificationStepResult `
    -Name "vulkan-direct-visual-parity" `
    -Status "Failed" `
    -StartedAtUtc $started `
    -FinishedAtUtc $finished `
    -Artifacts ([pscustomobject]@{ ReportPath = "vulkan-direct/matrix-report.json" }) `
    -ErrorMessage "Synthetic compatibility failure."
$skippedStep = New-RendererQualificationStepResult `
    -Name "build" `
    -Status "Skipped" `
    -StartedAtUtc $started `
    -FinishedAtUtc $started `
    -SkipReason "Synthetic no-build run."

$systemInfo = [pscustomobject]@{ ComputerName = "synthetic-host" }
$configuration = [pscustomobject]@{ Config = "Release" }
$failedReport = New-RendererQualificationReport `
    -StartedAtUtc $started `
    -FinishedAtUtc $finished `
    -SystemInfo $systemInfo `
    -Configuration $configuration `
    -Steps @($passedStep, $failedStep, $skippedStep)

Assert-Condition (-not $failedReport.Passed) (
    "A qualification report containing a failed step must fail.")
Assert-Condition (
    $failedReport.ExecutedStepCount -eq 2 -and
    $failedReport.PassedStepCount -eq 1 -and
    $failedReport.FailedStepCount -eq 1 -and
    $failedReport.SkippedStepCount -eq 1) (
    "Qualification report step counters are incorrect.")

$passingReport = New-RendererQualificationReport `
    -StartedAtUtc $started `
    -FinishedAtUtc $finished `
    -SystemInfo $systemInfo `
    -Configuration $configuration `
    -Steps @($passedStep, $skippedStep)
Assert-Condition $passingReport.Passed (
    "A qualification report with an executed passing step and no failures should pass.")

$allSkippedReport = New-RendererQualificationReport `
    -StartedAtUtc $started `
    -FinishedAtUtc $finished `
    -SystemInfo $systemInfo `
    -Configuration $configuration `
    -Steps @($skippedStep)
Assert-Condition (-not $allSkippedReport.Passed) (
    "A qualification report with no executed checks must not pass.")

$tempPath = Join-Path (
    [IO.Path]::GetTempPath()) (
    "pokemonautochess-renderer-qualification-" + [Guid]::NewGuid().ToString("N") + ".json")
try {
    $writtenPath = Write-RendererQualificationReport -Report $failedReport -Path $tempPath
    $roundTrip = Get-Content -LiteralPath $writtenPath -Raw | ConvertFrom-Json
    Assert-Condition (
        -not $roundTrip.Passed -and
        @($roundTrip.Steps).Count -eq 3 -and
        $roundTrip.Steps[1].ErrorMessage -eq "Synthetic compatibility failure.") (
        "Qualification JSON round trip did not preserve aggregate or step results.")
} finally {
    $resolvedTempPath = [IO.Path]::GetFullPath($tempPath)
    $resolvedSystemTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolvedTempPath.StartsWith(
            $resolvedSystemTemp,
            [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedTempPath -Force -ErrorAction SilentlyContinue
    }
}

$evidenceTempPath = Join-Path (
    [IO.Path]::GetTempPath()) (
    "pokemonautochess-renderer-evidence-" + [Guid]::NewGuid().ToString("N"))
try {
    $directPath = Join-Path $evidenceTempPath "direct"
    $nativePath = Join-Path $evidenceTempPath "native"
    $malformedPath = Join-Path $evidenceTempPath "malformed"
    New-Item -ItemType Directory -Path $directPath, $nativePath, $malformedPath -Force |
        Out-Null

    Set-Content `
        -LiteralPath (Join-Path $directPath "vulkan.stdout.log") `
        -Value (
            "[Vulkan] Initialized adapter='Synthetic Direct GPU' api=1.3 " +
            "swapchain=1x1 dualSourceBlend=1 descriptorIndexing=0 indirectWorld=0 vsync=0")
    Set-Content `
        -LiteralPath (Join-Path $nativePath "vulkan.stdout.log") `
        -Value (
            "[Vulkan] Initialized adapter='Synthetic Native GPU' api=1.3 " +
            "swapchain=1x1 dualSourceBlend=1 descriptorIndexing=1 indirectWorld=1 vsync=0")
    Set-Content `
        -LiteralPath (Join-Path $malformedPath "vulkan.stdout.log") `
        -Value "[Vulkan] Initialized without renderer mode fields"

    $modeEvidence = @(
        Get-RendererQualificationVulkanModeEvidence `
            -MatrixOutputDirectory $evidenceTempPath)
    Assert-Condition ($modeEvidence.Count -eq 2) (
        "Vulkan mode evidence must contain only parseable initialization records.")
    Assert-Condition (
        $modeEvidence[0].Scene -eq "direct" -and
        $modeEvidence[0].Adapter -eq "Synthetic Direct GPU" -and
        $modeEvidence[0].DescriptorIndexing -eq 0 -and
        $modeEvidence[0].IndirectWorld -eq 0) (
        "Vulkan direct-mode evidence was not parsed correctly.")
    Assert-Condition (
        $modeEvidence[1].Scene -eq "native" -and
        $modeEvidence[1].Adapter -eq "Synthetic Native GPU" -and
        $modeEvidence[1].DescriptorIndexing -eq 1 -and
        $modeEvidence[1].IndirectWorld -eq 1) (
        "Vulkan native-mode evidence was not parsed correctly.")
} finally {
    $resolvedEvidenceTempPath = [IO.Path]::GetFullPath($evidenceTempPath)
    $resolvedSystemTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolvedEvidenceTempPath.StartsWith(
            $resolvedSystemTemp,
            [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item `
            -LiteralPath $resolvedEvidenceTempPath `
            -Recurse `
            -Force `
            -ErrorAction SilentlyContinue
    }
}

Write-Host "[RendererQualificationReportTest] PASS"
