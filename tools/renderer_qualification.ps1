param(
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [string]$OutputDir = "debug/renderer_qualification",
    [string]$ManifestPath = "config/render_parity_scene_matrix.json",
    [int]$BackendContractAutoQuitSeconds = 2,
    [switch]$NoBuild,
    [switch]$SkipCapture,
    [switch]$ReportOnly
)

$ErrorActionPreference = "Stop"

Import-Module (Join-Path $PSScriptRoot "RendererQualificationReport.psm1") -Force
Import-Module (Join-Path $PSScriptRoot "RendererQualificationEvidence.psm1") -Force

$requiredBackends = @("opengl", "vulkan", "d3d12")
$compatibilityBackends = @("opengl", "vulkan")
$referenceBackend = "opengl"
$qualificationStartedAtUtc = [DateTime]::UtcNow
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildDirAbs = if ([IO.Path]::IsPathRooted($BuildDir)) {
    [IO.Path]::GetFullPath($BuildDir)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
}
$manifestAbs = if ([IO.Path]::IsPathRooted($ManifestPath)) {
    (Resolve-Path $ManifestPath).Path
} else {
    (Resolve-Path (Join-Path $repoRoot $ManifestPath)).Path
}
$outputDirAbs = if ([IO.Path]::IsPathRooted($OutputDir)) {
    [IO.Path]::GetFullPath($OutputDir)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDir))
}
$qualificationReportPath = Join-Path $outputDirAbs "qualification-report.json"
$backendContractReportPath = Join-Path $outputDirAbs "backend-contract.json"
$nativeOutputDir = Join-Path $outputDirAbs "native"
$nativeReportPath = Join-Path $nativeOutputDir "matrix-report.json"
$vulkanDirectOutputDir = Join-Path $outputDirAbs "vulkan-direct"
$vulkanDirectReportPath = Join-Path $vulkanDirectOutputDir "matrix-report.json"
$backendContractScript = Join-Path $PSScriptRoot "check_renderer_parity_contract.ps1"
$parityMatrixScript = Join-Path $PSScriptRoot "render_parity_matrix.ps1"
$qualificationSteps = @()

$vulkanDirectOverrides = [ordered]@{
    PAC_VULKAN_INDIRECT_WORLD_SCENE = "0"
    PAC_VULKAN_DISABLE_DESCRIPTOR_INDEXING = "1"
    PAC_VULKAN_DISABLE_INDIRECT_WORLD = "1"
}

function Get-GitValue {
    param([string[]]$Arguments)

    try {
        $value = @(& git @Arguments 2>$null)
        if ($LASTEXITCODE -eq 0 -and $value.Count -gt 0) {
            return [string]$value[-1]
        }
    } catch {
    }
    return $null
}

function Write-CurrentQualificationReport {
    $report = New-RendererQualificationReport `
        -StartedAtUtc $qualificationStartedAtUtc `
        -FinishedAtUtc ([DateTime]::UtcNow) `
        -SystemInfo $systemInfo `
        -Configuration $configuration `
        -Steps @($qualificationSteps)
    $null = Write-RendererQualificationReport `
        -Report $report `
        -Path $qualificationReportPath
    return $report
}

function Add-SkippedQualificationStep {
    param(
        [string]$Name,
        [string]$Reason,
        [AllowNull()]
        [object]$Artifacts
    )

    $now = [DateTime]::UtcNow
    $script:qualificationSteps += New-RendererQualificationStepResult `
        -Name $Name `
        -Status "Skipped" `
        -StartedAtUtc $now `
        -FinishedAtUtc $now `
        -Artifacts $Artifacts `
        -SkipReason $Reason
    $null = Write-CurrentQualificationReport
}

function Invoke-QualificationStep {
    param(
        [string]$Name,
        [AllowNull()]
        [object]$Artifacts,
        [scriptblock]$Action
    )

    Write-Host "[RendererQualification][$Name] START"
    $startedAtUtc = [DateTime]::UtcNow
    $status = "Passed"
    $errorMessage = $null
    try {
        & $Action | ForEach-Object { Write-Host ([string]$_) }
    } catch {
        $status = "Failed"
        $errorMessage = $_.Exception.Message
    }
    $finishedAtUtc = [DateTime]::UtcNow
    $script:qualificationSteps += New-RendererQualificationStepResult `
        -Name $Name `
        -Status $status `
        -StartedAtUtc $startedAtUtc `
        -FinishedAtUtc $finishedAtUtc `
        -Artifacts $Artifacts `
        -ErrorMessage $errorMessage
    $null = Write-CurrentQualificationReport

    if ($status -eq "Passed") {
        Write-Host (
            "[RendererQualification][$Name] PASS " +
            ("({0:N2}s)" -f ($finishedAtUtc - $startedAtUtc).TotalSeconds))
    } else {
        Write-Host "[RendererQualification][$Name] FAIL $errorMessage"
    }
}

function Invoke-WithProcessEnvironment {
    param(
        [System.Collections.IDictionary]$Overrides,
        [scriptblock]$Action
    )

    $backup = @{}
    try {
        foreach ($entry in $Overrides.GetEnumerator()) {
            $backup[$entry.Key] =
                [Environment]::GetEnvironmentVariable($entry.Key, "Process")
            [Environment]::SetEnvironmentVariable(
                $entry.Key,
                [string]$entry.Value,
                "Process")
        }
        & $Action
    } finally {
        foreach ($entry in $backup.GetEnumerator()) {
            [Environment]::SetEnvironmentVariable(
                $entry.Key,
                $entry.Value,
                "Process")
        }
    }
}

New-Item -ItemType Directory -Path $outputDirAbs -Force | Out-Null
$systemInfo = Get-RendererQualificationSystemInfo
$configuration = [pscustomobject]@{
    RepositoryRoot = $repoRoot
    GitBranch = Get-GitValue -Arguments @("branch", "--show-current")
    GitCommit = Get-GitValue -Arguments @("rev-parse", "HEAD")
    BuildDirectory = $buildDirAbs
    Config = $Config
    ManifestPath = $manifestAbs
    OutputDirectory = $outputDirAbs
    Backends = $requiredBackends
    ReferenceBackend = $referenceBackend
    NoBuild = $NoBuild.IsPresent
    SkipCapture = $SkipCapture.IsPresent
    VulkanDirectOverrides = [pscustomobject]$vulkanDirectOverrides
}

$null = Write-CurrentQualificationReport

Write-Host "[RendererQualification] Repo: $repoRoot"
Write-Host "[RendererQualification] Commit: $($configuration.GitCommit)"
Write-Host "[RendererQualification] Build: $buildDirAbs ($Config)"
Write-Host "[RendererQualification] Output: $outputDirAbs"
foreach ($adapter in @($systemInfo.DisplayAdapters)) {
    Write-Host (
        "[RendererQualification] GPU: $($adapter.Name) " +
        "driver=$($adapter.DriverVersion) status=$($adapter.Status)")
}
foreach ($warning in @($systemInfo.MetadataWarnings)) {
    Write-Host "[RendererQualification] Metadata warning: $warning"
}

$buildArtifacts = [pscustomobject]@{
    BuildDirectory = $buildDirAbs
    Config = $Config
    Target = "PokemonAutochess"
}
if ($NoBuild) {
    Add-SkippedQualificationStep `
        -Name "build" `
        -Reason "NoBuild was requested; the existing executable will be qualified." `
        -Artifacts $buildArtifacts
} else {
    Push-Location $repoRoot
    try {
        Invoke-QualificationStep `
            -Name "build" `
            -Artifacts $buildArtifacts `
            -Action {
                & cmake --build $buildDirAbs --config $Config --target PokemonAutochess |
                    ForEach-Object { Write-Host ([string]$_) }
                if ($LASTEXITCODE -ne 0) {
                    throw "Renderer qualification build failed with exit code $LASTEXITCODE."
                }
            }
    } finally {
        Pop-Location
    }
}

$buildFailed = @(
    $qualificationSteps |
        Where-Object { $_.Name -eq "build" -and $_.Status -eq "Failed" }).Count -gt 0

if ($buildFailed) {
    foreach ($stepName in @(
            "backend-contract",
            "native-visual-parity",
            "vulkan-direct-visual-parity")) {
        Add-SkippedQualificationStep `
            -Name $stepName `
            -Reason "The build step failed; stale or missing binaries were not qualified." `
            -Artifacts $null
    }
} else {
    $backendContractArtifacts = [pscustomobject]@{
        ReportPath = $backendContractReportPath
        Backends = $requiredBackends
        Summary = $null
    }
    if (Test-Path -LiteralPath $backendContractReportPath) {
        Remove-Item -LiteralPath $backendContractReportPath -Force
    }
    Push-Location $repoRoot
    try {
        Invoke-QualificationStep `
            -Name "backend-contract" `
            -Artifacts $backendContractArtifacts `
            -Action {
                & $backendContractScript `
                    -BuildDir $buildDirAbs `
                    -Config $Config `
                    -AutoQuitSeconds $BackendContractAutoQuitSeconds `
                    -Backends $requiredBackends `
                    -OutputPath $backendContractReportPath `
                    -ReportOnly
                if (-not (Test-Path -LiteralPath $backendContractReportPath)) {
                    throw "Backend contract report was not produced."
                }
                $backendContractReport =
                    Get-Content -LiteralPath $backendContractReportPath -Raw |
                        ConvertFrom-Json
                $backendContractArtifacts.Summary = [pscustomobject]@{
                    Passed = [bool]$backendContractReport.Passed
                    ExpectedSignature = $backendContractReport.ExpectedSignature
                    ResultCount = @($backendContractReport.Results).Count
                }
                if (-not $backendContractReport.Passed) {
                    throw "One or more renderer backend contracts failed."
                }
            }
    } finally {
        Pop-Location
    }

    $nativeArtifacts = [pscustomobject]@{
        OutputDirectory = $nativeOutputDir
        ReportPath = $nativeReportPath
        Backends = $requiredBackends
        Summary = $null
    }
    if (Test-Path -LiteralPath $nativeReportPath) {
        Remove-Item -LiteralPath $nativeReportPath -Force
    }
    Push-Location $repoRoot
    try {
        Invoke-QualificationStep `
            -Name "native-visual-parity" `
            -Artifacts $nativeArtifacts `
            -Action {
                $matrixArgs = @{
                    BuildDir = $buildDirAbs
                    Config = $Config
                    OutputDir = $nativeOutputDir
                    ManifestPath = $manifestAbs
                    Backends = $requiredBackends
                    ReferenceBackend = $referenceBackend
                    ReportOnly = $true
                }
                if ($SkipCapture) {
                    $matrixArgs.SkipCapture = $true
                }
                & $parityMatrixScript @matrixArgs
                if (-not (Test-Path -LiteralPath $nativeReportPath)) {
                    throw "Native visual parity report was not produced."
                }
                $nativeReport =
                    Get-Content -LiteralPath $nativeReportPath -Raw |
                        ConvertFrom-Json
                $nativeVulkanModes = @(
                    Get-RendererQualificationVulkanModeEvidence `
                        -MatrixOutputDirectory $nativeOutputDir)
                $nativeArtifacts.Summary = [pscustomobject]@{
                    Passed = [bool]$nativeReport.Passed
                    SceneCount = [int]$nativeReport.SceneCount
                    PassedCount = [int]$nativeReport.PassedCount
                    FailedCount = [int]$nativeReport.FailedCount
                    VulkanModes = $nativeVulkanModes
                }
                if (-not $nativeReport.Passed) {
                    throw "Native three-backend visual parity matrix failed."
                }
            }
    } finally {
        Pop-Location
    }

    $vulkanDirectArtifacts = [pscustomobject]@{
        OutputDirectory = $vulkanDirectOutputDir
        ReportPath = $vulkanDirectReportPath
        Backends = $compatibilityBackends
        EnvironmentOverrides = [pscustomobject]$vulkanDirectOverrides
        Summary = $null
    }
    if (Test-Path -LiteralPath $vulkanDirectReportPath) {
        Remove-Item -LiteralPath $vulkanDirectReportPath -Force
    }
    Push-Location $repoRoot
    try {
        Invoke-QualificationStep `
            -Name "vulkan-direct-visual-parity" `
            -Artifacts $vulkanDirectArtifacts `
            -Action {
                Invoke-WithProcessEnvironment `
                    -Overrides $vulkanDirectOverrides `
                    -Action {
                        $matrixArgs = @{
                            BuildDir = $buildDirAbs
                            Config = $Config
                            OutputDir = $vulkanDirectOutputDir
                            ManifestPath = $manifestAbs
                            Backends = $compatibilityBackends
                            ReferenceBackend = $referenceBackend
                            ReportOnly = $true
                        }
                        if ($SkipCapture) {
                            $matrixArgs.SkipCapture = $true
                        }
                        & $parityMatrixScript @matrixArgs
                    }
                if (-not (Test-Path -LiteralPath $vulkanDirectReportPath)) {
                    throw "Vulkan direct compatibility report was not produced."
                }
                $vulkanDirectReport =
                    Get-Content -LiteralPath $vulkanDirectReportPath -Raw |
                        ConvertFrom-Json
                $vulkanDirectModes = @(
                    Get-RendererQualificationVulkanModeEvidence `
                        -MatrixOutputDirectory $vulkanDirectOutputDir)
                $vulkanDirectArtifacts.Summary = [pscustomobject]@{
                    Passed = [bool]$vulkanDirectReport.Passed
                    SceneCount = [int]$vulkanDirectReport.SceneCount
                    PassedCount = [int]$vulkanDirectReport.PassedCount
                    FailedCount = [int]$vulkanDirectReport.FailedCount
                    VulkanModes = $vulkanDirectModes
                }
                if (-not $vulkanDirectReport.Passed) {
                    throw "Forced Vulkan direct compatibility visual parity matrix failed."
                }
                $incorrectModeCount = @(
                    $vulkanDirectModes |
                        Where-Object {
                            $_.DescriptorIndexing -ne 0 -or
                            $_.IndirectWorld -ne 0
                        }).Count
                if ($vulkanDirectModes.Count -ne [int]$vulkanDirectReport.SceneCount -or
                    $incorrectModeCount -ne 0) {
                    throw (
                        "Forced Vulkan direct compatibility mode was not proven " +
                        "for every scene: evidence=$($vulkanDirectModes.Count) " +
                        "scenes=$($vulkanDirectReport.SceneCount) " +
                        "incorrect=$incorrectModeCount.")
                }
            }
    } finally {
        Pop-Location
    }
}

$finalReport = Write-CurrentQualificationReport
Write-Host "[RendererQualification] Report: $qualificationReportPath"
Write-Host (
    "[RendererQualification] Steps: passed=$($finalReport.PassedStepCount) " +
    "failed=$($finalReport.FailedStepCount) skipped=$($finalReport.SkippedStepCount)")

if ($finalReport.Passed) {
    Write-Host "[RendererQualification] PASS"
} elseif ($ReportOnly) {
    Write-Host "[RendererQualification] REPORT ONLY: qualification failed."
} else {
    $failedNames = @(
        $finalReport.Steps |
            Where-Object { $_.Status -eq "Failed" } |
            ForEach-Object { $_.Name })
    throw "Renderer qualification failed: $($failedNames -join ', '). Inspect qualification-report.json."
}
