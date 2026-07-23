param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug",
    [string]$OutputDir = "debug/render_parity_matrix",
    [string]$ManifestPath = "config/render_parity_scene_matrix.json",
    [string[]]$Backends = @("opengl", "vulkan", "d3d12"),
    [string]$ReferenceBackend = "opengl",
    [string[]]$Cases = @(),
    [switch]$SkipCapture,
    [switch]$ReportOnly,
    [switch]$ListCases
)

$ErrorActionPreference = "Stop"

Import-Module (Join-Path $PSScriptRoot "RenderParitySceneManifest.psm1") -Force

if ($Backends -notcontains $ReferenceBackend) {
    throw "Reference backend '$ReferenceBackend' must be included in -Backends."
}
if ($Backends.Count -lt 2) {
    throw "At least two backends are required for the render parity matrix."
}

$repoRoot = (Resolve-Path ".").Path
$manifestAbs = (Resolve-Path $ManifestPath).Path
$manifest = Import-RenderParitySceneManifest -Path $manifestAbs -RepoRoot $repoRoot
$scenes = @(Select-RenderParityScenes -Scenes @($manifest.scenes) -RequestedCases $Cases)

if ($ListCases) {
    foreach ($scene in $scenes) {
        Write-Host ("{0}: {1} [{2}]" -f `
            $scene.name,
            $scene.focus,
            (@($scene.coverage) -join ", "))
    }
    return
}

$outputDirAbs = Resolve-RenderParityRepoPath -RepoRoot $repoRoot -Path $OutputDir
New-Item -ItemType Directory -Path $outputDirAbs -Force | Out-Null
$singleSceneScript = Join-Path $PSScriptRoot "render_parity_screenshot_diff.ps1"
$capture = $manifest.capture
$thresholds = $manifest.thresholds
$sceneResults = @()
$matrixFailed = $false

Write-Host "[RenderParityMatrix] Manifest: $manifestAbs"
Write-Host "[RenderParityMatrix] Output: $outputDirAbs"
Write-Host "[RenderParityMatrix] Scenes: $($scenes.name -join ', ')"

foreach ($scene in $scenes) {
    $sceneOutputDir = Join-Path $OutputDir $scene.name
    $sceneOutputDirAbs = Resolve-RenderParityRepoPath `
        -RepoRoot $repoRoot `
        -Path $sceneOutputDir
    $sceneReportPath = Join-Path $sceneOutputDirAbs "report.json"
    if (Test-Path -LiteralPath $sceneReportPath) {
        Remove-Item -LiteralPath $sceneReportPath -Force
    }

    $snapshotPath = ""
    if ($null -ne $scene.snapshotPath -and
        -not [string]::IsNullOrWhiteSpace([string]$scene.snapshotPath)) {
        $snapshotPath = [string]$scene.snapshotPath
    }

    $autoQuitSeconds = [int]$capture.autoQuitSeconds
    if ($null -ne $scene.autoQuitSeconds) {
        $autoQuitSeconds = [int]$scene.autoQuitSeconds
    }

    $sceneArgs = @{
        BuildDir = $BuildDir
        Config = $Config
        OutputDir = $sceneOutputDir
        SceneName = [string]$scene.name
        SceneFocus = [string]$scene.focus
        SnapshotPath = $snapshotPath
        Backends = $Backends
        ReferenceBackend = $ReferenceBackend
        Width = [int]$capture.width
        Height = [int]$capture.height
        ScreenshotFrame = [int]$scene.screenshotFrame
        FixedFrameDtSeconds = [double]$capture.fixedFrameDtSeconds
        AutoQuitSeconds = $autoQuitSeconds
        WaitTimeoutSeconds = [int]$capture.waitTimeoutSeconds
        MeanAbsoluteErrorThreshold = [double]$thresholds.meanAbsoluteError
        RootMeanSquareErrorThreshold = [double]$thresholds.rootMeanSquareError
        ChangedPixelRatioThreshold = [double]$thresholds.changedPixelRatio
        PixelChannelTolerance = [int]$thresholds.pixelChannelTolerance
        HeatmapScale = [int]$thresholds.heatmapScale
        ReportOnly = $true
    }
    if ($SkipCapture) {
        $sceneArgs.SkipCapture = $true
    }

    $infrastructureError = $null
    try {
        & $singleSceneScript @sceneArgs
    } catch {
        $infrastructureError = $_.Exception.Message
    }

    $sceneReport = $null
    if ($null -eq $infrastructureError -and (Test-Path -LiteralPath $sceneReportPath)) {
        $sceneReport = Get-Content -LiteralPath $sceneReportPath -Raw | ConvertFrom-Json
    } elseif ($null -eq $infrastructureError) {
        $infrastructureError = "Scene report was not produced: $sceneReportPath"
    }

    $scenePassed = $null -eq $infrastructureError -and $sceneReport.Passed
    $matrixFailed = $matrixFailed -or -not $scenePassed
    $sceneResults += [pscustomobject]@{
        Name = [string]$scene.name
        Focus = [string]$scene.focus
        Coverage = @($scene.coverage)
        ReportPath = $sceneReportPath
        Passed = $scenePassed
        InfrastructureError = $infrastructureError
        Capture = if ($null -eq $sceneReport) { $null } else {
            [pscustomobject]@{
                SnapshotPath = $sceneReport.SnapshotPath
                Width = $sceneReport.Width
                Height = $sceneReport.Height
                ScreenshotFrame = $sceneReport.ScreenshotFrame
                FixedFrameDtSeconds = $sceneReport.FixedFrameDtSeconds
            }
        }
        Results = if ($null -eq $sceneReport) { @() } else { @($sceneReport.Results) }
    }

    if ($scenePassed) {
        Write-Host "[RenderParityMatrix][$($scene.name)] PASS"
    } elseif ($null -eq $infrastructureError) {
        Write-Host "[RenderParityMatrix][$($scene.name)] FAIL image thresholds exceeded"
    } else {
        Write-Host "[RenderParityMatrix][$($scene.name)] FAIL $infrastructureError"
    }
}

$passedCount = @($sceneResults | Where-Object { $_.Passed }).Count
$matrixReport = [pscustomobject]@{
    CapturedAtUtc = [DateTime]::UtcNow.ToString("o")
    ManifestPath = $manifestAbs
    ReferenceBackend = $ReferenceBackend
    Backends = $Backends
    Thresholds = $thresholds
    SceneCount = $sceneResults.Count
    PassedCount = $passedCount
    FailedCount = $sceneResults.Count - $passedCount
    Passed = -not $matrixFailed
    Scenes = $sceneResults
}
$matrixReportPath = Join-Path $outputDirAbs "matrix-report.json"
$matrixReport | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath $matrixReportPath -Encoding UTF8
Write-Host "[RenderParityMatrix] Report: $matrixReportPath"

if ($matrixFailed -and -not $ReportOnly) {
    throw "Renderer parity matrix failed. Inspect matrix-report.json and per-scene heatmaps."
}
if ($matrixFailed) {
    Write-Host "[RenderParityMatrix] REPORT ONLY: one or more scenes failed"
} else {
    Write-Host "[RenderParityMatrix] PASS ($passedCount/$($sceneResults.Count) scenes)"
}
