param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug",
    [string]$OutputDir = "debug/render_parity",
    [string]$SceneName = "single-scene",
    [string]$SceneFocus = "ad-hoc renderer comparison",
    [AllowEmptyString()]
    [string]$SnapshotPath = "config/debug/debug_state_snapshot_tail_fire_starter_line.json",
    [string[]]$Backends = @("opengl", "vulkan", "d3d12"),
    [string]$ReferenceBackend = "opengl",
    [int]$Width = 1280,
    [int]$Height = 720,
    [int]$ScreenshotFrame = 120,
    [double]$FixedFrameDtSeconds = (1.0 / 60.0),
    [int]$AutoQuitSeconds = 8,
    [int]$WaitTimeoutSeconds = 75,
    [double]$MeanAbsoluteErrorThreshold = 0.01,
    [double]$RootMeanSquareErrorThreshold = 0.08,
    [double]$ChangedPixelRatioThreshold = 0.05,
    [int]$PixelChannelTolerance = 8,
    [int]$HeatmapScale = 4,
    [object[]]$ContentGuards = @(),
    [switch]$SkipCapture,
    [switch]$ReportOnly
)

$ErrorActionPreference = "Stop"

Import-Module (Join-Path $PSScriptRoot "RenderParityImageDiff.psm1") -Force
Import-Module (Join-Path $PSScriptRoot "RenderParityContentGuard.psm1") -Force

function Resolve-GameExePath {
    param(
        [string]$BuildDir,
        [string]$Config
    )

    $candidateA = Join-Path $BuildDir "$Config/PokemonAutochess.exe"
    if (Test-Path $candidateA) { return (Resolve-Path $candidateA).Path }

    $candidateB = Join-Path $BuildDir "PokemonAutochess.exe"
    if (Test-Path $candidateB) { return (Resolve-Path $candidateB).Path }

    throw "PokemonAutochess.exe not found under '$BuildDir' (config '$Config')."
}

function Set-CaptureEnvVar {
    param(
        [string]$Name,
        [AllowNull()]
        [string]$Value,
        [hashtable]$Backup
    )

    if (-not $Backup.ContainsKey($Name)) {
        $Backup[$Name] = [Environment]::GetEnvironmentVariable($Name, "Process")
    }
    [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
}

function Restore-CaptureEnv {
    param([hashtable]$Backup)

    foreach ($entry in $Backup.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, "Process")
    }
}

function Invoke-BackendScreenshot {
    param(
        [string]$ExePath,
        [string]$Backend,
        [string]$OutputDir,
        [string]$SnapshotPath,
        [int]$Width,
        [int]$Height,
        [int]$ScreenshotFrame,
        [double]$FixedFrameDtSeconds,
        [int]$AutoQuitSeconds,
        [int]$WaitTimeoutSeconds
    )

    $backup = @{}
    $screenshotPath = Join-Path $OutputDir "$Backend.png"
    $stdoutPath = Join-Path $OutputDir "$Backend.stdout.log"
    $stderrPath = Join-Path $OutputDir "$Backend.stderr.log"
    $fixedFrameDt = $FixedFrameDtSeconds.ToString("R", [Globalization.CultureInfo]::InvariantCulture)

    try {
        Set-CaptureEnvVar -Name "PAC_RENDER_BACKEND" -Value $Backend -Backup $backup
        Set-CaptureEnvVar -Name "PAC_RANDOM_SEED" -Value "12345" -Backup $backup
        Set-CaptureEnvVar -Name "PAC_VIDEO_WIDTH" -Value "$Width" -Backup $backup
        Set-CaptureEnvVar -Name "PAC_VIDEO_HEIGHT" -Value "$Height" -Backup $backup
        Set-CaptureEnvVar -Name "PAC_VIDEO_FULLSCREEN" -Value "0" -Backup $backup
        Set-CaptureEnvVar -Name "PAC_VIDEO_VSYNC" -Value "0" -Backup $backup
        Set-CaptureEnvVar -Name "PAC_VIDEO_FPS_CAP" -Value "0" -Backup $backup
        Set-CaptureEnvVar -Name "PAC_FIXED_FRAME_DT_SECONDS" -Value $fixedFrameDt -Backup $backup
        if ([string]::IsNullOrWhiteSpace($SnapshotPath)) {
            Set-CaptureEnvVar -Name "PAC_DEBUG_STATE_PATH" -Value $null -Backup $backup
            Set-CaptureEnvVar -Name "PAC_AUTO_LOAD_DEBUG_SNAPSHOT" -Value $null -Backup $backup
            Set-CaptureEnvVar -Name "PAC_PIN_DEBUG_SNAPSHOT_STATE" -Value $null -Backup $backup
        } else {
            Set-CaptureEnvVar -Name "PAC_DEBUG_STATE_PATH" -Value $SnapshotPath -Backup $backup
            Set-CaptureEnvVar -Name "PAC_AUTO_LOAD_DEBUG_SNAPSHOT" -Value "1" -Backup $backup
            Set-CaptureEnvVar -Name "PAC_PIN_DEBUG_SNAPSHOT_STATE" -Value "1" -Backup $backup
        }
        Set-CaptureEnvVar -Name "PAC_AUTO_QUIT_SECONDS" -Value "$AutoQuitSeconds" -Backup $backup
        Set-CaptureEnvVar -Name "PAC_AUTO_QUIT_FRAMES" -Value "$($ScreenshotFrame + 2)" -Backup $backup
        Set-CaptureEnvVar -Name "PAC_BACKEND_SCREENSHOT_PATH" -Value $screenshotPath -Backup $backup
        Set-CaptureEnvVar -Name "PAC_BACKEND_SCREENSHOT_FRAME" -Value "$ScreenshotFrame" -Backup $backup
        Set-CaptureEnvVar -Name "PAC_PARITY_CONTRACT_FATAL" -Value "1" -Backup $backup

        foreach ($path in @($screenshotPath, $stdoutPath, $stderrPath)) {
            if (Test-Path $path) { Remove-Item $path -Force }
        }

        $process = Start-Process -FilePath $ExePath `
            -WorkingDirectory (Resolve-Path ".").Path `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath `
            -PassThru

        if (-not $process.WaitForExit($WaitTimeoutSeconds * 1000)) {
            Stop-Process -Id $process.Id -Force
            throw "Screenshot capture for backend '$Backend' timed out."
        }
        $process.WaitForExit()
        $process.Refresh()
        $exitCode = $process.ExitCode
        if ($null -ne $exitCode -and $exitCode -ne 0) {
            throw "Screenshot capture for backend '$Backend' exited with code $($process.ExitCode)."
        }
        if (-not (Test-Path $screenshotPath)) {
            throw "Screenshot file not produced for backend '$Backend': $screenshotPath"
        }

        $stdout = @(Get-Content $stdoutPath -ErrorAction SilentlyContinue)
        $shotLine = $stdout | Where-Object { $_ -match "^\[Screenshot\]" } | Select-Object -Last 1
        if (-not $shotLine) {
            throw "No successful screenshot log line observed for backend '$Backend'."
        }
        Write-Host "[RenderParity][$Backend] $shotLine"
    } finally {
        Restore-CaptureEnv -Backup $backup
    }
}

if ($Backends -notcontains $ReferenceBackend) {
    throw "Reference backend '$ReferenceBackend' must be included in -Backends."
}
if ($Backends.Count -lt 2) {
    throw "At least two backends are required for screenshot parity comparison."
}

$exePath = Resolve-GameExePath -BuildDir $BuildDir -Config $Config
$repoRoot = (Resolve-Path ".").Path
if ([IO.Path]::IsPathRooted($OutputDir)) {
    $outputDirAbs = [IO.Path]::GetFullPath($OutputDir)
} else {
    $outputDirAbs = [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDir))
}
$snapshotAbs = $null
if (-not [string]::IsNullOrWhiteSpace($SnapshotPath)) {
    $snapshotAbs = (Resolve-Path $SnapshotPath).Path
}
New-Item -ItemType Directory -Path $outputDirAbs -Force | Out-Null

Write-Host "[RenderParity] EXE: $exePath"
Write-Host "[RenderParity] Scene: $SceneName ($SceneFocus)"
if ($null -eq $snapshotAbs) {
    Write-Host "[RenderParity] Snapshot: <none; startup scene>"
} else {
    Write-Host "[RenderParity] Snapshot: $snapshotAbs"
}
Write-Host "[RenderParity] Output dir: $outputDirAbs"
Write-Host "[RenderParity] Capture: ${Width}x${Height} frame=$ScreenshotFrame fixedDt=$FixedFrameDtSeconds"

if (-not $SkipCapture) {
    foreach ($backend in $Backends) {
        Invoke-BackendScreenshot `
            -ExePath $exePath `
            -Backend $backend `
            -OutputDir $outputDirAbs `
            -SnapshotPath $snapshotAbs `
            -Width $Width `
            -Height $Height `
            -ScreenshotFrame $ScreenshotFrame `
            -FixedFrameDtSeconds $FixedFrameDtSeconds `
            -AutoQuitSeconds $AutoQuitSeconds `
            -WaitTimeoutSeconds $WaitTimeoutSeconds
    }
}

$referencePath = Join-Path $outputDirAbs "$ReferenceBackend.png"
if (-not (Test-Path $referencePath)) {
    throw "Reference screenshot does not exist: $referencePath"
}

$results = @()
$contentGuardResults = @()
$failed = $false
foreach ($backend in $Backends) {
    $imagePath = Join-Path $outputDirAbs "$backend.png"
    if (-not (Test-Path $imagePath)) {
        throw "Screenshot does not exist for backend '$backend': $imagePath"
    }

    foreach ($guard in $ContentGuards) {
        $metrics = Test-RenderParityImageContent -ImagePath $imagePath -Guard $guard
        if ($null -eq $metrics) {
            throw "Content guard returned no metrics for '$backend/$($guard.name)'."
        }

        $failed = $failed -or -not $metrics.Passed
        $result = [pscustomobject]@{
            Backend = $backend
            Name = $metrics.Name
            ImagePath = $imagePath
            X = $metrics.X
            Y = $metrics.Y
            Width = $metrics.Width
            Height = $metrics.Height
            PixelCount = $metrics.PixelCount
            MeanLuminance = $metrics.MeanLuminance
            LuminanceStandardDeviation = $metrics.LuminanceStandardDeviation
            NearBlackPixelRatio = $metrics.NearBlackPixelRatio
            MidtonePixelRatio = $metrics.MidtonePixelRatio
            NearBlackLuminanceMaximum = $metrics.NearBlackLuminanceMaximum
            MidtoneLuminanceMinimum = $metrics.MidtoneLuminanceMinimum
            MidtoneLuminanceMaximum = $metrics.MidtoneLuminanceMaximum
            MaximumNearBlackPixelRatio = $metrics.MaximumNearBlackPixelRatio
            MinimumMidtonePixelRatio = $metrics.MinimumMidtonePixelRatio
            FailureReasons = @($metrics.FailureReasons)
            Passed = $metrics.Passed
        }
        $contentGuardResults += $result

        Write-Host (
            "[RenderParity][$backend/$($result.Name)] " +
            ("midtone={0:P2}>={1:P2} near-black={2:P2}<={3:P2} pass={4}" -f `
                $result.MidtonePixelRatio,
                $result.MinimumMidtonePixelRatio,
                $result.NearBlackPixelRatio,
                $result.MaximumNearBlackPixelRatio,
                $result.Passed))
    }
}

foreach ($backend in $Backends) {
    if ($backend -eq $ReferenceBackend) { continue }

    $candidatePath = Join-Path $outputDirAbs "$backend.png"
    if (-not (Test-Path $candidatePath)) {
        throw "Candidate screenshot does not exist: $candidatePath"
    }
    $pairName = "$ReferenceBackend-$backend"
    $heatmapPath = Join-Path $outputDirAbs "$pairName.heatmap.png"
    $metrics = Compare-RenderParityImages `
        -ReferencePath $referencePath `
        -CandidatePath $candidatePath `
        -HeatmapPath $heatmapPath `
        -PixelChannelTolerance $PixelChannelTolerance `
        -HeatmapScale $HeatmapScale
    if ($null -eq $metrics) {
        throw "Image comparison returned no metrics for '$pairName'."
    }

    $pairFailed =
        $metrics.MeanAbsoluteError -gt $MeanAbsoluteErrorThreshold -or
        $metrics.RootMeanSquareError -gt $RootMeanSquareErrorThreshold -or
        $metrics.ChangedPixelRatio -gt $ChangedPixelRatioThreshold
    $failed = $failed -or $pairFailed

    $result = [pscustomobject]@{
        Pair = $pairName
        ReferencePath = $referencePath
        CandidatePath = $candidatePath
        HeatmapPath = $heatmapPath
        Width = $metrics.Width
        Height = $metrics.Height
        MeanAbsoluteError = $metrics.MeanAbsoluteError
        RootMeanSquareError = $metrics.RootMeanSquareError
        MaxChannelError = $metrics.MaxChannelError
        ChangedPixelRatio = $metrics.ChangedPixelRatio
        PixelChannelTolerance = $metrics.PixelChannelTolerance
        Passed = -not $pairFailed
    }
    $results += $result

    Write-Host (
        "[RenderParity][$pairName] " +
        ("mae={0:N6} rmse={1:N6} max={2:N6} changed>{3}={4:P2} pass={5}" -f `
            $result.MeanAbsoluteError,
            $result.RootMeanSquareError,
            $result.MaxChannelError,
            $PixelChannelTolerance,
            $result.ChangedPixelRatio,
            $result.Passed))
}

$report = [pscustomobject]@{
    CapturedAtUtc = [DateTime]::UtcNow.ToString("o")
    SceneName = $SceneName
    SceneFocus = $SceneFocus
    SnapshotPath = $snapshotAbs
    ReferenceBackend = $ReferenceBackend
    Backends = $Backends
    Width = $Width
    Height = $Height
    ScreenshotFrame = $ScreenshotFrame
    FixedFrameDtSeconds = $FixedFrameDtSeconds
    Thresholds = [pscustomobject]@{
        MeanAbsoluteError = $MeanAbsoluteErrorThreshold
        RootMeanSquareError = $RootMeanSquareErrorThreshold
        ChangedPixelRatio = $ChangedPixelRatioThreshold
        PixelChannelTolerance = $PixelChannelTolerance
    }
    Results = $results
    ContentGuardResults = $contentGuardResults
    Passed = -not $failed
}
$reportPath = Join-Path $outputDirAbs "report.json"
$report | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Host "[RenderParity] Report: $reportPath"

if ($failed -and -not $ReportOnly) {
    throw "Renderer screenshot parity or expected-content thresholds exceeded. Inspect report.json and the generated heatmaps."
}
if ($failed) {
    Write-Host "[RenderParity] REPORT ONLY: thresholds exceeded"
} else {
    Write-Host "[RenderParity] PASS"
}
