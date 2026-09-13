[CmdletBinding()]
param(
    [string]$OutputDirectory = 'debug/editor-workflow',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
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
Import-Module (Join-Path $taskRoot 'tools/RenderParityImageDiff.psm1') -Force
Import-Module (Join-Path $taskRoot 'tools/RenderParityContentGuard.psm1') -Force
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
            $taskRunOutput = Join-Path $taskCaseOutput $taskBackend
            $taskState = Join-Path $taskRunOutput 'state'
            New-Item -ItemType Directory -Path $taskState -Force | Out-Null
            $taskScreenshot = Join-Path $taskRunOutput 'capture.png'
            $taskMetricsPath = Join-Path $taskRunOutput 'metrics.json'
            if (-not $SkipCapture) {
                $taskProject.startup_scene.scene_id = $taskCase.sceneId
                # Keep the descriptor at the project root so content paths resolve normally.
                $taskDescriptor = Join-Path $taskRoot ".phlosion.workflow.$($taskCase.name).$taskBackend.project.json"
                $taskProject | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $taskDescriptor
                $env:PAC_RANDOM_SEED = '12345'
                $env:PHLOSION_BACKEND_SCREENSHOT_DEFER = $null
                $env:PHLOSION_BACKEND_SCREENSHOT_PATH = $taskScreenshot
                $env:PHLOSION_BACKEND_SCREENSHOT_FRAME = '164'
                foreach ($taskOld in @($taskScreenshot, $taskMetricsPath)) {
                    if (Test-Path -LiteralPath $taskOld) { Remove-Item -LiteralPath $taskOld }
                }
                $taskArguments = @("--project=$taskDescriptor", "--renderer=$taskBackend", '--hidden', '--no-auto-reload',
                    '--frames=169', '--fixed-delta=0.016666667', "--state-directory=$taskState", "--metrics-output=$taskMetricsPath")
                if ($taskCase.scenario) { $taskArguments += "--game-preview=$($taskCase.scenario)" }
                if ($taskCase.play) { $taskArguments += '--play-game-preview' }
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
            $taskContents = $taskMetrics.project.editor_contents
            if ($taskMetrics.renderer.backend -ne $taskBackend -or -not $taskMetrics.capture.hidden -or
                $taskMetrics.project.active_scene.id -ne $taskCase.sceneId -or $taskMetrics.project.visible_triangles -le 0) {
                throw "Wrong renderer, location or missing environment: $($taskCase.name)/$taskBackend"
            }
            if ($taskContents.scenes.Count -ne 5 -or $taskContents.scenarios.Count -ne 23 -or
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
            $taskGuard = Test-RenderParityImageContent -ImagePath $taskScreenshot -Guard ([pscustomobject]@{
                name='visible-environment'; x=.3; y=.23; width=.4; height=.43;
                maximumNearBlackPixelRatio=.12; minimumMidtonePixelRatio=.3
            })
            if (-not $taskGuard.Passed) { throw "Missing scene content: $($taskCase.name)/$taskBackend" }
            $taskContent += [pscustomobject]@{ backend=$taskBackend; unitCount=$taskUnits.Count; passed=$true; imageGuard=$taskGuard }
        }
        $taskPairs = foreach ($taskBackend in @('vulkan', 'd3d12')) {
            $taskDiff = Compare-RenderParityImages -ReferencePath (Join-Path $taskCaseOutput 'opengl/capture.png') `
                -CandidatePath (Join-Path $taskCaseOutput "$taskBackend/capture.png") -PixelChannelTolerance $taskImageLimits.pixelChannelTolerance
            $taskPassed = $taskDiff.MeanAbsoluteError -le $taskImageLimits.meanAbsoluteError -and
                $taskDiff.RootMeanSquareError -le $taskImageLimits.rootMeanSquareError -and
                $taskDiff.ChangedPixelRatio -le $taskImageLimits.changedPixelRatio
            [pscustomobject]@{ pair="opengl-$taskBackend"; passed=$taskPassed; metrics=$taskDiff }
        }
        $taskPassed = @($taskPairs | Where-Object { -not $_.passed }).Count -eq 0
        $taskResults += [pscustomobject]@{ name=$taskCase.name; passed=$taskPassed; content=$taskContent; pairs=$taskPairs }
        $taskResults | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $taskOutput 'report.json')
        if (-not $taskPassed) { throw "Editor image parity failed: $($taskCase.name)" }
    }
    Write-Host "[EditorWorkflow] PASS: $($taskResults.Count) cases on all three native APIs."
} finally {
    foreach ($taskKey in $taskPrevious.Keys) { [Environment]::SetEnvironmentVariable($taskKey, $taskPrevious[$taskKey], 'Process') }
    Pop-Location
}
