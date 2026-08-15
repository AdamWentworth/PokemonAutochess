[CmdletBinding()]
param(
    [string]$GameRoot = '',
    [string]$EngineRoot = '',
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$OutputDirectory = '',
    [string]$AssetQuery = '0025_Pikachu_SV.phlo',
    [string]$AssetPreviewAnimation = 'bind',
    [ValidateSet('composite', 'raw-base-color', 'albedo', 'resolved-albedo', 'normal', 'roughness', 'metallic', 'ao', 'emissive')]
    [string]$AssetPreviewMaterialView = 'composite',
    [ValidateRange(0.0, 3600.0)]
    [double]$AssetPreviewTime = 0.0,
    [ValidateRange(0.0, 20.0)]
    [double]$AssetPreviewZoom = 0.0,
    [ValidateRange(-2.0, 2.0)]
    [double]$AssetPreviewTargetOffsetY = 0.0,
    [switch]$AssetPreviewFront,
    [switch]$AssetPreviewBack,
    [ValidateSet('opengl', 'd3d12', 'vulkan')]
    [string[]]$Backends = @('opengl', 'd3d12', 'vulkan'),
    [ValidateSet('low', 'medium', 'high', 'ultra')]
    [string[]]$Qualities = @('low', 'medium', 'high', 'ultra'),
    [ValidateRange(20, 1000)]
    [int]$FrameCount = 90,
    [ValidateRange(1, 999)]
    [int]$ScreenshotFrame = 60,
    [ValidateRange(10, 600)]
    [int]$TimeoutSeconds = 180,
    [switch]$SkipRoute1,
    [switch]$SkipPairVerification
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$PathValue)
    return [IO.Path]::GetFullPath($PathValue)
}

function Get-RelativePortablePath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$PathValue
    )

    $rootPath = (Resolve-FullPath $Root).TrimEnd('\', '/')
    $fullPath = Resolve-FullPath $PathValue
    $prefix = $rootPath + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith(
            $prefix,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside the baseline output directory: $fullPath"
    }
    return $fullPath.Substring($prefix.Length).Replace('\', '/')
}

function Get-PngDimensions {
    param([Parameter(Mandatory = $true)][string]$PathValue)

    $bytes = [IO.File]::ReadAllBytes($PathValue)
    if ($bytes.Length -lt 24 -or
        $bytes[0] -ne 137 -or $bytes[1] -ne 80 -or
        $bytes[2] -ne 78 -or $bytes[3] -ne 71) {
        throw "Capture is not a valid PNG: $PathValue"
    }
    $width = [int](
        ([uint32]$bytes[16] -shl 24) -bor
        ([uint32]$bytes[17] -shl 16) -bor
        ([uint32]$bytes[18] -shl 8) -bor
        [uint32]$bytes[19])
    $height = [int](
        ([uint32]$bytes[20] -shl 24) -bor
        ([uint32]$bytes[21] -shl 16) -bor
        ([uint32]$bytes[22] -shl 8) -bor
        [uint32]$bytes[23])
    return [pscustomobject]@{ width = $width; height = $height }
}

function Get-GitProvenance {
    param([Parameter(Mandatory = $true)][string]$Root)

    return [pscustomobject][ordered]@{
        commit = [string](& git -C $Root rev-parse HEAD)
        branch = [string](& git -C $Root branch --show-current)
        status = @(& git -C $Root status --short | ForEach-Object { [string]$_ })
    }
}

function Invoke-HiddenEditor {
    param(
        [Parameter(Mandatory = $true)][string]$EditorPath,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$ScreenshotPath,
        [Parameter(Mandatory = $true)][string]$StandardOutputPath,
        [Parameter(Mandatory = $true)][string]$StandardErrorPath,
        [Parameter(Mandatory = $true)][int]$CaptureFrame,
        [Parameter(Mandatory = $true)][int]$ProcessTimeoutSeconds
    )

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $EditorPath
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.Arguments = @($Arguments | ForEach-Object {
        # ProcessStartInfo.ArgumentList is unavailable in Windows PowerShell's
        # .NET Framework. These automation arguments never contain literal
        # quotes or a trailing directory separator, so standard CRT quoting is
        # unambiguous here.
        if ($_ -match '"') {
            throw "Editor argument contains an unsupported quote: $_"
        }
        '"' + $_ + '"'
    }) -join ' '
    $startInfo.EnvironmentVariables['PHLOSION_BACKEND_SCREENSHOT_PATH'] =
        $ScreenshotPath
    $startInfo.EnvironmentVariables['PHLOSION_BACKEND_SCREENSHOT_FRAME'] =
        [string]$CaptureFrame
    $startInfo.EnvironmentVariables['PHLOSION_REQUIRE_COOKED_ASSETS'] = '1'
    $startInfo.EnvironmentVariables['PHLOSION_TRACE_ASSET_LOADS'] = '1'

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    $peakWorkingSetBytes = [int64]0
    try {
        if (-not $process.Start()) {
            throw "Could not start hidden editor process: $EditorPath"
        }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        while (-not $process.HasExited) {
            if ($stopwatch.Elapsed.TotalSeconds -gt $ProcessTimeoutSeconds) {
                try { $process.Kill($true) } catch { $process.Kill() }
                throw "Hidden editor timed out after $ProcessTimeoutSeconds seconds."
            }
            try {
                $process.Refresh()
                $peakWorkingSetBytes = [Math]::Max(
                    $peakWorkingSetBytes,
                    [int64]$process.WorkingSet64)
            } catch {
                # The process can exit between HasExited and Refresh.
            }
            Start-Sleep -Milliseconds 50
        }
        $process.WaitForExit()
        $standardOutput = $stdoutTask.GetAwaiter().GetResult()
        $standardError = $stderrTask.GetAwaiter().GetResult()
        [IO.File]::WriteAllText($StandardOutputPath, $standardOutput)
        [IO.File]::WriteAllText($StandardErrorPath, $standardError)
        return [pscustomobject][ordered]@{
            exit_code = $process.ExitCode
            elapsed_ms = $stopwatch.Elapsed.TotalMilliseconds
            peak_working_set_bytes = $peakWorkingSetBytes
        }
    } finally {
        $stopwatch.Stop()
        $process.Dispose()
    }
}

if ($ScreenshotFrame -ge $FrameCount) {
    throw 'ScreenshotFrame must be less than FrameCount.'
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $scriptRoot '..\..'
}
$GameRoot = Resolve-FullPath $GameRoot
if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
    $EngineRoot = Join-Path $GameRoot '..\..\Phlosion\PhlosionEngine'
}
$EngineRoot = Resolve-FullPath $EngineRoot
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmssZ')
    $OutputDirectory = Join-Path $GameRoot "artifacts\baselines\editor-$stamp"
}
$OutputDirectory = Resolve-FullPath $OutputDirectory
$projectPath = Join-Path $GameRoot 'phlosion.project.json'
$editorPath = Join-Path $EngineRoot "build\$Configuration\PhlosionEditor.exe"

if ($AssetPreviewFront -and $AssetPreviewBack) {
    throw 'AssetPreviewFront and AssetPreviewBack are mutually exclusive.'
}

foreach ($requiredPath in @($GameRoot, $EngineRoot)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Container)) {
        throw "Required directory does not exist: $requiredPath"
    }
}
foreach ($requiredPath in @($projectPath, $editorPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required file does not exist: $requiredPath"
    }
}

if (Test-Path -LiteralPath $OutputDirectory -PathType Container) {
    $existingOutput = @(Get-ChildItem -LiteralPath $OutputDirectory -Force)
    if ($existingOutput.Count -gt 0) {
        throw "Baseline output directory must be new or empty: $OutputDirectory"
    }
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
if (-not $SkipPairVerification) {
    $proofDirectory = Join-Path $OutputDirectory 'pair-proof'
    & (Join-Path $scriptRoot 'build_editor_pair.ps1') `
        -GameRoot $GameRoot `
        -EngineRoot $EngineRoot `
        -Configuration $Configuration `
        -OutputDirectory $proofDirectory `
        -VerifyOnly | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw 'The paired editor/plugin proof failed; baseline capture was not started.'
    }
}

$qualityIndex = @{
    low = 0
    medium = 1
    high = 2
    ultra = 3
}
$runs = @()
$captureCountPerBackend =
    $Qualities.Count + $(if ($SkipRoute1) { 0 } else { 1 })
$totalRuns = $Backends.Count * $captureCountPerBackend
$runNumber = 0

foreach ($backend in $Backends) {
    $captures = @($Qualities | ForEach-Object {
        [pscustomobject]@{ kind = 'inspector'; quality = $_ }
    })
    if (-not $SkipRoute1) {
        $captures += [pscustomobject]@{
            kind = 'route1'
            quality = $null
        }
    }
    foreach ($capture in $captures) {
        ++$runNumber
        $name = if ($capture.kind -eq 'inspector') {
            "inspector-$($capture.quality)"
        } else {
            'route1'
        }
        Write-Host "[$runNumber/$totalRuns] Capturing $backend $name in a hidden window..."
        $runDirectory = Join-Path $OutputDirectory "$backend\$name"
        $stateDirectory = Join-Path $runDirectory 'state'
        New-Item -ItemType Directory -Path $stateDirectory -Force | Out-Null
        $screenshotPath = Join-Path $runDirectory 'capture.png'
        $metricsPath = Join-Path $runDirectory 'editor_metrics.json'
        $stdoutPath = Join-Path $runDirectory 'stdout.log'
        $stderrPath = Join-Path $runDirectory 'stderr.log'
        $editorArguments = @(
            "--project=$projectPath",
            "--renderer=$backend",
            '--hidden',
            "--state-directory=$stateDirectory",
            "--metrics-output=$metricsPath",
            '--fixed-delta=0.016666667',
            "--frames=$FrameCount"
        )
        if ($capture.kind -eq 'inspector') {
            $editorArguments += @(
                "--asset-preview=$AssetQuery",
                "--asset-preview-quality=$($capture.quality)",
                "--asset-preview-material-view=$AssetPreviewMaterialView",
                "--asset-preview-animation=$AssetPreviewAnimation",
                "--asset-preview-time=$($AssetPreviewTime.ToString([Globalization.CultureInfo]::InvariantCulture))"
            )
            if ($AssetPreviewZoom -gt 0.0) {
                $editorArguments +=
                    "--asset-preview-zoom=$($AssetPreviewZoom.ToString([Globalization.CultureInfo]::InvariantCulture))"
            }
            if ($AssetPreviewTargetOffsetY -ne 0.0) {
                $editorArguments +=
                    "--asset-preview-target-offset-y=$($AssetPreviewTargetOffsetY.ToString([Globalization.CultureInfo]::InvariantCulture))"
            }
            if ($AssetPreviewFront) {
                $editorArguments += '--asset-preview-front'
            }
            if ($AssetPreviewBack) {
                $editorArguments += '--asset-preview-back'
            }
        }

        $processResult = Invoke-HiddenEditor `
            -EditorPath $editorPath `
            -WorkingDirectory $GameRoot `
            -Arguments $editorArguments `
            -ScreenshotPath $screenshotPath `
            -StandardOutputPath $stdoutPath `
            -StandardErrorPath $stderrPath `
            -CaptureFrame $ScreenshotFrame `
            -ProcessTimeoutSeconds $TimeoutSeconds
        if ($processResult.exit_code -ne 0) {
            throw "Hidden editor failed for $backend $name (exit $($processResult.exit_code)); inspect $stderrPath"
        }
        foreach ($requiredOutput in @($screenshotPath, $metricsPath)) {
            if (-not (Test-Path -LiteralPath $requiredOutput -PathType Leaf) -or
                (Get-Item -LiteralPath $requiredOutput).Length -eq 0) {
                throw "Hidden editor did not produce output: $requiredOutput"
            }
        }

        $dimensions = Get-PngDimensions $screenshotPath
        if ($dimensions.width -ne 1440 -or $dimensions.height -ne 900) {
            throw "Unexpected capture dimensions for $backend ${name}: $($dimensions.width)x$($dimensions.height)"
        }
        $editorMetrics = Get-Content -LiteralPath $metricsPath -Raw |
            ConvertFrom-Json
        if (-not $editorMetrics.capture.hidden) {
            throw "Editor did not confirm hidden mode for $backend $name."
        }
        if ([string]$editorMetrics.renderer.backend -ne $backend) {
            throw "Renderer fallback detected for $backend ${name}: $($editorMetrics.renderer.backend)"
        }
        if ($capture.kind -eq 'inspector') {
            $expectedQuality = [int]$qualityIndex[$capture.quality]
            if (-not $editorMetrics.asset_preview.ready -or
                [int]$editorMetrics.asset_preview.graphics_quality -ne $expectedQuality) {
                throw "Inspector quality contract failed for $backend $($capture.quality)."
            }
        } elseif ([string]$editorMetrics.project.active_scene.id -ne 'routes/route1') {
            throw "Route 1 baseline opened '$($editorMetrics.project.active_scene.id)' instead."
        }

        $screenshotFile = Get-Item -LiteralPath $screenshotPath
        $runs += [pscustomobject][ordered]@{
            backend = $backend
            kind = $capture.kind
            quality = $capture.quality
            screenshot = Get-RelativePortablePath $OutputDirectory $screenshotPath
            screenshot_sha256 = (Get-FileHash -LiteralPath $screenshotPath -Algorithm SHA256).Hash.ToLowerInvariant()
            screenshot_bytes = [int64]$screenshotFile.Length
            width = $dimensions.width
            height = $dimensions.height
            editor_metrics = Get-RelativePortablePath $OutputDirectory $metricsPath
            stdout = Get-RelativePortablePath $OutputDirectory $stdoutPath
            stderr = Get-RelativePortablePath $OutputDirectory $stderrPath
            elapsed_ms = $processResult.elapsed_ms
            peak_working_set_bytes = $processResult.peak_working_set_bytes
            project_load_ms = [double]$editorMetrics.project.load_total_ms
            steady_cpu_frame_p95_ms = [double]$editorMetrics.renderer.cpu_frame_steady.p95_ms
            steady_gpu_frame_p95_ms = if ($null -ne $editorMetrics.renderer.gpu_frame_steady.p95_ms) {
                [double]$editorMetrics.renderer.gpu_frame_steady.p95_ms
            } else {
                $null
            }
        }
    }

    $qualityHashes = @($runs |
        Where-Object { $_.backend -eq $backend -and $_.kind -eq 'inspector' } |
        Select-Object -ExpandProperty screenshot_sha256 -Unique)
    if ($qualityHashes.Count -ne $Qualities.Count) {
        throw "One or more Inspector quality captures are byte-identical for $backend."
    }
}

Import-Module `
    (Join-Path $scriptRoot '..\RenderParityImageDiff.psm1') `
    -Force
$comparisonDirectory = Join-Path $OutputDirectory 'comparisons'
New-Item -ItemType Directory -Path $comparisonDirectory -Force |
    Out-Null
$comparisons = @()
foreach ($backend in $Backends) {
    $lowCapture = @($runs | Where-Object {
        $_.backend -eq $backend -and
        $_.kind -eq 'inspector' -and
        $_.quality -eq 'low'
    }) | Select-Object -First 1
    $ultraCapture = @($runs | Where-Object {
        $_.backend -eq $backend -and
        $_.kind -eq 'inspector' -and
        $_.quality -eq 'ultra'
    }) | Select-Object -First 1
    if ($null -eq $lowCapture -or $null -eq $ultraCapture) {
        continue
    }
    $heatmapPath = Join-Path `
        $comparisonDirectory `
        "$backend-low-vs-ultra.png"
    $difference = Compare-RenderParityImages `
        -ReferencePath (Join-Path $OutputDirectory $lowCapture.screenshot) `
        -CandidatePath (Join-Path $OutputDirectory $ultraCapture.screenshot) `
        -HeatmapPath $heatmapPath `
        -PixelChannelTolerance 2 `
        -HeatmapScale 8
    if ($difference.ChangedPixelRatio -lt 0.0005) {
        throw "Low-to-Ultra visual difference is too small for $backend ($($difference.ChangedPixelRatio))."
    }
    $comparisons += [pscustomobject][ordered]@{
        backend = $backend
        reference_quality = 'low'
        candidate_quality = 'ultra'
        pixel_channel_tolerance = $difference.PixelChannelTolerance
        mean_absolute_error = $difference.MeanAbsoluteError
        root_mean_square_error = $difference.RootMeanSquareError
        max_channel_error = $difference.MaxChannelError
        changed_pixel_ratio = $difference.ChangedPixelRatio
        heatmap = Get-RelativePortablePath $OutputDirectory $heatmapPath
    }
}

$manifest = [pscustomobject][ordered]@{
    schema = 'pokemon-autochess-editor-baseline-v1'
    generated_at_utc = [DateTime]::UtcNow.ToString('o')
    contract = [pscustomobject][ordered]@{
        hidden_window = $true
        isolated_state = $true
        fixed_delta_seconds = 1.0 / 60.0
        vsync = $false
        frame_count = $FrameCount
        screenshot_frame = $ScreenshotFrame
        configuration = $Configuration
        asset_query = $AssetQuery
        asset_preview_animation = $AssetPreviewAnimation
        asset_preview_time = $AssetPreviewTime
        asset_preview_zoom = $AssetPreviewZoom
        asset_preview_target_offset_y = $AssetPreviewTargetOffsetY
        asset_preview_front = [bool]$AssetPreviewFront
        asset_preview_back = [bool]$AssetPreviewBack
        backends = $Backends
        qualities = $Qualities
    }
    provenance = [pscustomobject][ordered]@{
        game = Get-GitProvenance $GameRoot
        engine = Get-GitProvenance $EngineRoot
        editor_sha256 = (Get-FileHash -LiteralPath $editorPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    runs = $runs
    comparisons = $comparisons
}
$manifestPath = Join-Path $OutputDirectory 'baseline.json'
$reportPath = Join-Path $OutputDirectory 'baseline.md'
$manifest | ConvertTo-Json -Depth 12 |
    Set-Content -LiteralPath $manifestPath -Encoding UTF8

$builder = [Text.StringBuilder]::new()
[void]$builder.AppendLine('# Phlosion Editor Visual and Performance Baseline')
[void]$builder.AppendLine()
[void]$builder.AppendLine("Generated: $($manifest.generated_at_utc)")
[void]$builder.AppendLine()
[void]$builder.AppendLine('All captures used a hidden SDL window, isolated editor state, a fixed 1/60-second clock, and vsync disabled. Each requested backend was required to remain active without fallback.')
[void]$builder.AppendLine()
[void]$builder.AppendLine('| Backend | View | Quality | Load (ms) | Steady CPU p95 (ms) | Steady GPU p95 (ms) | Peak working set | Capture |')
[void]$builder.AppendLine('| --- | --- | --- | ---: | ---: | ---: | ---: | --- |')
foreach ($run in $runs) {
    $quality = if ($null -eq $run.quality) { '-' } else { $run.quality }
    $gpu = if ($null -eq $run.steady_gpu_frame_p95_ms) { 'n/a' } else { '{0:N3}' -f $run.steady_gpu_frame_p95_ms }
    $memory = '{0:N1} MiB' -f ($run.peak_working_set_bytes / 1MB)
    [void]$builder.AppendLine("| $($run.backend) | $($run.kind) | $quality | $([Math]::Round($run.project_load_ms, 2)) | $([Math]::Round($run.steady_cpu_frame_p95_ms, 3)) | $gpu | $memory | [PNG]($($run.screenshot)) |")
}
foreach ($backend in $Backends) {
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("## $backend")
    [void]$builder.AppendLine()
    foreach ($run in @($runs | Where-Object backend -eq $backend)) {
        $label = if ($run.kind -eq 'route1') { 'Route 1' } else { "Inspector $($run.quality)" }
        [void]$builder.AppendLine("### $label")
        [void]$builder.AppendLine()
        [void]$builder.AppendLine("![${backend} ${label}]($($run.screenshot))")
        [void]$builder.AppendLine()
    }
}
[void]$builder.AppendLine()
[void]$builder.AppendLine('## Low versus Ultra visual differences')
[void]$builder.AppendLine()
[void]$builder.AppendLine('Heatmaps are amplified 8x. The gate requires changes beyond the quality selector label so the model itself must respond to the quality policy.')
[void]$builder.AppendLine()
foreach ($comparison in $comparisons) {
    [void]$builder.AppendLine("### $($comparison.backend)")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("Changed pixels: $([Math]::Round($comparison.changed_pixel_ratio * 100.0, 3))%")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("![$($comparison.backend) Low versus Ultra heatmap]($($comparison.heatmap))")
    [void]$builder.AppendLine()
}
Set-Content -LiteralPath $reportPath -Value $builder.ToString() -Encoding UTF8

Write-Host "Baseline complete: $($runs.Count) validated hidden captures."
Write-Host "Manifest: $manifestPath"
Write-Host "Report: $reportPath"

[pscustomobject]@{
    ManifestPath = $manifestPath
    ReportPath = $reportPath
    RunCount = $runs.Count
}
