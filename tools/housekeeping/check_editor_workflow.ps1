[CmdletBinding()]
param(
    [string]$OutputDirectory = 'debug/editor-workflow',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')][string]$Configuration = 'RelWithDebInfo',
    [string[]]$Cases = @(),
    [switch]$SkipCapture
)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskEngine = [IO.Path]::GetFullPath((Join-Path $taskRoot '../../Phlosion/PhlosionEngine'))
$taskEditor = Join-Path $taskEngine "build/$Configuration/PhlosionEditor.exe"
$taskOutput = [IO.Path]::GetFullPath([IO.Path]::Combine($taskRoot, $OutputDirectory))
$taskManifest = Get-Content (Join-Path $taskRoot 'config/editor/workflow_capture_matrix.json') -Raw | ConvertFrom-Json
$taskImageLimits = (Get-Content (Join-Path $taskRoot 'config/render_parity_scene_matrix.json') -Raw | ConvertFrom-Json).thresholds
$taskProject = Get-Content (Join-Path $taskRoot 'phlosion.project.json') -Raw | ConvertFrom-Json
$taskRuns = @($taskManifest.cases | Where-Object { -not $Cases.Count -or $_.name -in $Cases })
if (-not $taskRuns.Count -or @($Cases | Where-Object { $_ -notin $taskManifest.cases.name }).Count) { throw 'Unknown editor capture case.' }
# Transition cases compare against a fresh load of the same destination.
$taskRequestedNames = @($taskRuns.name) + @($taskRuns | ForEach-Object { $_.referenceCase } | Where-Object { $_ })
$taskRuns = @($taskManifest.cases | Where-Object { $_.name -in $taskRequestedNames })
Import-Module (Join-Path $taskRoot 'tools/RenderParityImageDiff.psm1') -Force
Import-Module (Join-Path $taskRoot 'tools/RenderParityContentGuard.psm1') -Force
function Export-EnvironmentComparisonImage([string]$Source, [string]$Destination) {
    $taskBitmap = [Drawing.Bitmap]::new($Source)
    try {
        # Same environment interior as the content guard; excludes changing UI
        # labels/status and keeps the board, grass and surrounding real shadows.
        $taskRectangle = [Drawing.Rectangle]::new(
            [int]($taskBitmap.Width * .3), [int]($taskBitmap.Height * .23),
            [int]($taskBitmap.Width * .4), [int]($taskBitmap.Height * .43))
        $taskCrop = $taskBitmap.Clone($taskRectangle, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try { $taskCrop.Save($Destination, [Drawing.Imaging.ImageFormat]::Png) } finally { $taskCrop.Dispose() }
    } finally { $taskBitmap.Dispose() }
}
$taskResults = @()
$taskPrevious = @{}
foreach ($taskKey in @('PAC_RANDOM_SEED', 'PHLOSION_BACKEND_SCREENSHOT_PATH', 'PHLOSION_BACKEND_SCREENSHOT_FRAME', 'PHLOSION_BACKEND_SCREENSHOT_DEFER')) {
    $taskPrevious[$taskKey] = [Environment]::GetEnvironmentVariable($taskKey, 'Process')
}
Push-Location $taskRoot
try {
    foreach ($taskCase in $taskRuns) {
        $taskCaseOutput = Join-Path $taskOutput $taskCase.name
        $taskContent = @()
        foreach ($taskBackend in @('opengl', 'vulkan', 'd3d12')) {
            $taskCaptureFrame = if ($taskCase.captureFrame) { [int]$taskCase.captureFrame } else { 164 }
            $taskRunOutput = Join-Path $taskCaseOutput $taskBackend
            $taskState = Join-Path $taskRunOutput 'state'
            New-Item -ItemType Directory -Path $taskState -Force | Out-Null
            $taskScreenshot = Join-Path $taskRunOutput 'capture.png'
            $taskMetricsPath = Join-Path $taskRunOutput 'metrics.json'
            if (-not $SkipCapture) {
                $taskProject.startup_scene.scene_id = if ($taskCase.startupSceneId) { $taskCase.startupSceneId } else { $taskCase.sceneId }
                # Keep the descriptor at the project root so content paths resolve normally.
                $taskDescriptor = Join-Path $taskRoot ".phlosion.workflow.$($taskCase.name).$taskBackend.project.json"
                $taskProject | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $taskDescriptor
                $env:PAC_RANDOM_SEED = '12345'
                $env:PHLOSION_BACKEND_SCREENSHOT_DEFER = $null
                $env:PHLOSION_BACKEND_SCREENSHOT_PATH = $taskScreenshot
                $env:PHLOSION_BACKEND_SCREENSHOT_FRAME = [string]$taskCaptureFrame
                foreach ($taskOld in @($taskScreenshot, $taskMetricsPath)) {
                    if (Test-Path -LiteralPath $taskOld) { Remove-Item -LiteralPath $taskOld }
                }
                $taskArguments = @("--project=$taskDescriptor", "--renderer=$taskBackend", '--hidden', '--no-auto-reload',
                    "--frames=$($taskCaptureFrame+5)", '--fixed-delta=0.016666667', "--state-directory=$taskState", "--metrics-output=$taskMetricsPath")
                if ($taskCase.scenario) { $taskArguments += "--game-preview=$($taskCase.scenario)" }
                if ($taskCase.play) { $taskArguments += '--play-game-preview' }
                if ($taskCase.stats) { $taskArguments += '--stats' }
                if ($taskCase.recording) {
                    $taskArguments += @('--record-performance-at=40','--record-performance-seconds=0.25',
                        '--record-performance-warmup-seconds=0.1',"--performance-output=$taskRunOutput/performance.json")
                }
                foreach ($taskOpen in $taskCase.sceneOpens) {
                    $taskArguments += "--open-scene-at=$($taskOpen.frame):$($taskOpen.sceneId)"
                }
                Write-Host "[EditorWorkflow] $($taskCase.name)/$taskBackend"
                $taskProcess = Start-Process -FilePath $taskEditor -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru `
                    -ArgumentList ($taskArguments | ForEach-Object { '"' + $_ + '"' }) `
                    -RedirectStandardOutput (Join-Path $taskRunOutput 'stdout.log') -RedirectStandardError (Join-Path $taskRunOutput 'stderr.log')
                $null = $taskProcess.Handle
                if (-not $taskProcess.WaitForExit(240000)) { Stop-Process -Id $taskProcess.Id; throw 'Editor capture timed out.' }
                $taskProcess.WaitForExit()
                if ($taskProcess.ExitCode -ne 0) { throw "Editor exited with $($taskProcess.ExitCode)." }
            }
            $taskMetrics = Get-Content -LiteralPath $taskMetricsPath -Raw | ConvertFrom-Json
            # Read plain .NET strings: Windows PowerShell's Get-Content adds
            # provider metadata which ConvertTo-Json recursively serializes.
            $taskSwitchLines = @([IO.File]::ReadAllLines((Join-Path $taskRunOutput 'stdout.log')) | Where-Object { $_ -match '^\[Phlosion Editor\]\[SceneSwitch\]' })
            $taskExpectedSwitches = @($taskCase.sceneOpens | Where-Object { $_ } | ForEach-Object {
                "[Phlosion Editor][SceneSwitch] frame=$($_.frame) scene=$($_.sceneId)"
            })
            if (($taskSwitchLines -join "`n") -cne ($taskExpectedSwitches -join "`n")) {
                throw "Scene switches did not execute as requested: $($taskCase.name)/$taskBackend"
            }
            $taskContents = $taskMetrics.project.editor_contents
            if ($taskCase.recording) {
                $taskRecording = Get-Content (Join-Path $taskRunOutput 'performance.json') -Raw | ConvertFrom-Json
                if ($taskRecording.schema -ne 'phlosion-performance-recording-v1' -or
                    $taskRecording.context.backend -ne $taskBackend -or
                    $taskRecording.context.build_configuration -ne $Configuration -or
                    $taskRecording.context.scene -ne $taskCase.sceneId -or
                    $taskRecording.frame.samples -lt 1 -or $taskRecording.gpu.samples -lt 1 -or
                    $taskRecording.frame.mean_ms -le 0 -or $taskRecording.frames.Count -ne $taskRecording.frame.samples) {
                    throw "Invalid performance recording: $($taskCase.name)/$taskBackend"
                }
            }
            if ($taskCase.stats -and (-not $taskMetrics.capture.stats_requested -or
                $taskMetrics.renderer.live_stats.fps -le 0 -or
                -not $taskMetrics.renderer.live_stats.gpu_valid)) {
                throw "The editor performance HUD has no live timing data: $($taskCase.name)/$taskBackend"
            }
            if ($taskMetrics.renderer.backend -ne $taskBackend -or -not $taskMetrics.capture.hidden -or
                $taskMetrics.project.active_scene.id -ne $taskCase.sceneId -or $taskMetrics.project.visible_triangles -le 0) {
                throw "Wrong renderer, location or missing environment: $($taskCase.name)/$taskBackend"
            }
            if ($taskContents.scenes.Count -ne 5 -or $taskContents.scenarios.Count -ne 25 -or
                @($taskContents.scenes | Where-Object { $_.category -notin @('Route 1', 'Experiments') }).Count) {
                throw 'The editor exposed retired scenes or scenarios.'
            }
            $taskObjects = @($taskContents.layout_objects)
            $taskBoard = @($taskObjects | Where-Object { $_.id -eq 'gameplay/autochess-board' })
            $taskUnits = @($taskObjects | Where-Object { $_.id -ne 'gameplay/autochess-board' })
            if ($taskBoard.Count -ne 1 -or $taskBoard[0].capabilities -ne 0 -or $taskBoard[0].viewport_mask -ne 0 -or
                @($taskUnits | Where-Object { $_.viewport_mask -ne 2 -or $_.capabilities -eq 0 }).Count -or
                $taskUnits.Count -ne [int]$taskCase.unitCount) {
                throw "Scenery handles leaked into the viewport, or Pokemon editing was lost: $($taskCase.name)/$taskBackend"
            }
            $taskImageGuard = if ($taskCase.imageGuard) { $taskCase.imageGuard } else { [pscustomobject]@{
                name='visible-environment'; x=.3; y=.23; width=.4; height=.43;
                maximumNearBlackPixelRatio=.12; minimumMidtonePixelRatio=.3
            } }
            $taskGuard = Test-RenderParityImageContent -ImagePath $taskScreenshot -Guard $taskImageGuard
            if (-not $taskGuard.Passed) { throw "Missing scene content: $($taskCase.name)/$taskBackend" }
            $taskFeatureGuards = @()
            foreach ($taskFeature in $taskCase.contentGuards) {
                $taskFeatureResult = Test-RenderParityImageContent -ImagePath $taskScreenshot -Guard $taskFeature
                if (-not $taskFeatureResult.Passed -or
                    $taskFeatureResult.LuminanceStandardDeviation -lt $taskFeature.minimumLuminanceStandardDeviation -or
                    $taskFeatureResult.MeanLuminance -lt $taskFeature.minimumMeanLuminance) {
                    throw "Missing expected scene feature '$($taskFeature.name)': $($taskCase.name)/$taskBackend"
                }
                $taskFeatureGuards += $taskFeatureResult
            }
            $taskEnvironmentImage = Join-Path $taskRunOutput 'environment.png'
            Export-EnvironmentComparisonImage $taskScreenshot $taskEnvironmentImage
            $taskHistoryDiff = $null
            $taskHistoryPassed = $true
            if ($taskCase.referenceCase) {
                $taskReferenceImage = Join-Path $taskOutput "$($taskCase.referenceCase)/$taskBackend/environment.png"
                $taskHistoryDiff = Compare-RenderParityImages -ReferencePath $taskReferenceImage -CandidatePath $taskEnvironmentImage `
                    -PixelChannelTolerance 0 -HeatmapPath (Join-Path $taskRunOutput 'scene-history-diff.png')
                # The stopped scene, camera and native API are identical. Scene
                # history must have no effect at all on the rendered environment.
                $taskHistoryPassed = $taskHistoryDiff.MaxChannelError -eq 0
            }
            $taskContent += [pscustomobject]@{ backend=$taskBackend; unitCount=$taskUnits.Count; passed=$taskHistoryPassed;
                imageGuard=$taskGuard; featureGuards=$taskFeatureGuards; sceneSwitches=$taskSwitchLines; sceneHistoryDiff=$taskHistoryDiff }
        }
        $taskPairs = foreach ($taskBackend in @('vulkan', 'd3d12')) {
            $taskDiff = Compare-RenderParityImages -ReferencePath (Join-Path $taskCaseOutput 'opengl/capture.png') `
                -CandidatePath (Join-Path $taskCaseOutput "$taskBackend/capture.png") -PixelChannelTolerance $taskImageLimits.pixelChannelTolerance
            $taskPassed = $taskDiff.MeanAbsoluteError -le $taskImageLimits.meanAbsoluteError -and
                $taskDiff.RootMeanSquareError -le $taskImageLimits.rootMeanSquareError -and
                $taskDiff.ChangedPixelRatio -le $taskImageLimits.changedPixelRatio
            [pscustomobject]@{ pair="opengl-$taskBackend"; passed=$taskPassed; metrics=$taskDiff }
        }
        $taskPassed = @($taskPairs | Where-Object { -not $_.passed }).Count -eq 0 -and
            @($taskContent | Where-Object { -not $_.passed }).Count -eq 0
        $taskResults += [pscustomobject]@{ name=$taskCase.name; passed=$taskPassed; content=$taskContent; pairs=$taskPairs }
        $taskResults | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $taskOutput 'report.json')
        if (-not $taskPassed) { throw "Editor image parity or scene-history comparison failed: $($taskCase.name)" }
    }
    Write-Host "[EditorWorkflow] PASS: $($taskResults.Count) cases on all three native APIs."
} finally {
    foreach ($taskKey in $taskPrevious.Keys) { [Environment]::SetEnvironmentVariable($taskKey, $taskPrevious[$taskKey], 'Process') }
    Pop-Location
}
