[CmdletBinding()]
param(
    [string]$OutputDirectory = 'debug/character-materials',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')][string]$Configuration = 'Release',
    [string]$ProjectDescriptor = 'phlosion.project.json',
    [string]$EngineRoot = '',
    [string]$BaselineDirectory = '',
    [string[]]$Backends = @('opengl', 'vulkan', 'd3d12'),
    [string[]]$Cases = @(),
    [switch]$SkipCapture
)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskEngine = if ($EngineRoot) { [IO.Path]::GetFullPath($EngineRoot) } else { [IO.Path]::GetFullPath((Join-Path $taskRoot '../../Phlosion/PhlosionEngine')) }
$taskEditor = Join-Path $taskEngine "build/$Configuration/PhlosionEditor.exe"
$taskOutput = [IO.Path]::GetFullPath([IO.Path]::Combine($taskRoot, $OutputDirectory))
$taskProject = [IO.Path]::GetFullPath([IO.Path]::Combine($taskRoot, $ProjectDescriptor))
$taskMatrix = Get-Content (Join-Path $taskRoot 'config/render/character_material_matrix.json') -Raw | ConvertFrom-Json
$taskLimits = (Get-Content (Join-Path $taskRoot 'config/render_parity_scene_matrix.json') -Raw | ConvertFrom-Json).thresholds
$taskRuns = @($taskMatrix.cases | Where-Object { -not $Cases.Count -or $_.name -in $Cases })
if (-not $taskRuns.Count -or @($Cases | Where-Object { $_ -notin $taskMatrix.cases.name }).Count) { throw 'Unknown character-material case.' }
if ($Backends -notcontains 'opengl' -or $Backends.Count -lt 2 -or @($Backends | Where-Object { $_ -notin @('opengl', 'vulkan', 'd3d12') }).Count) {
    throw 'Choose OpenGL and at least one supported comparison backend.'
}
Import-Module (Join-Path $taskRoot 'tools/RenderParityImageDiff.psm1') -Force
Import-Module (Join-Path $taskRoot 'tools/RenderParityContentGuard.psm1') -Force
$taskResults = @()
$taskPrior = @{}
foreach ($taskKey in @('PHLOSION_BACKEND_SCREENSHOT_PATH', 'PHLOSION_BACKEND_SCREENSHOT_FRAME', 'PHLOSION_BACKEND_SCREENSHOT_DEFER')) {
    $taskPrior[$taskKey] = [Environment]::GetEnvironmentVariable($taskKey, 'Process')
}
try {
    foreach ($taskCase in $taskRuns) {
        if (-not @($taskCase.contentGuards).Count) { throw "Missing expected-content guards: $($taskCase.name)" }
        $taskObjects = @(Get-ChildItem (Join-Path $taskRoot 'content/phlosion/objects') -Directory -Filter "$($taskCase.model)-*" |
            Where-Object { Test-Path (Join-Path $_.FullName "$($taskCase.model).phlo") })
        if ($taskObjects.Count -ne 1) { throw "Expected one published object for $($taskCase.model), found $($taskObjects.Count)." }
        $taskObject = "content/phlosion/objects/$($taskObjects[0].Name)/$($taskCase.model).phlo"
        $taskCaseOutput = Join-Path $taskOutput $taskCase.name
        $taskContent = @()
        foreach ($taskBackend in $Backends) {
            $taskRun = Join-Path $taskCaseOutput $taskBackend
            $taskState = Join-Path $taskRun 'state'
            New-Item -ItemType Directory -Path $taskState -Force | Out-Null
            $taskImage = Join-Path $taskRun 'capture.png'
            $taskMetricsPath = Join-Path $taskRun 'metrics.json'
            if (-not $SkipCapture) {
                Copy-Item (Join-Path $taskRoot 'config/editor/material_capture_layout.ini') (Join-Path $taskState 'layout.ini')
                foreach ($taskOld in @($taskImage, $taskMetricsPath)) {
                    if (Test-Path -LiteralPath $taskOld) { Remove-Item -LiteralPath $taskOld }
                }
                $env:PHLOSION_BACKEND_SCREENSHOT_PATH = $taskImage
                $env:PHLOSION_BACKEND_SCREENSHOT_FRAME = '60'
                $env:PHLOSION_BACKEND_SCREENSHOT_DEFER = $null
                $taskArguments = @("--project=$taskProject", "--renderer=$taskBackend", '--hidden', '--no-auto-reload',
                    '--frames=66', '--fixed-delta=0.016666667', "--state-directory=$taskState", "--metrics-output=$taskMetricsPath",
                    "--asset-preview=$taskObject", "--asset-preview-animation=$($taskCase.animation)", "--asset-preview-time=$($taskCase.time)",
                    '--asset-preview-quality=ultra', '--asset-preview-material-view=composite', "--asset-preview-lighting=$($taskCase.lighting)",
                    "--asset-preview-zoom=$($taskCase.zoom)", "--asset-preview-target-offset-y=$($taskCase.targetOffsetY)")
                if ($taskCase.view) { $taskArguments += "--asset-preview-$($taskCase.view)" }
                $taskProcess = Start-Process -FilePath $taskEditor -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru `
                    -ArgumentList ($taskArguments | ForEach-Object { '"' + $_ + '"' }) `
                    -RedirectStandardOutput (Join-Path $taskRun 'stdout.log') -RedirectStandardError (Join-Path $taskRun 'stderr.log')
                $null = $taskProcess.Handle
                if (-not $taskProcess.WaitForExit(240000)) { Stop-Process -Id $taskProcess.Id; throw "Character capture timed out: $($taskCase.name)/$taskBackend" }
                $taskProcess.WaitForExit()
                if ($taskProcess.ExitCode -ne 0) { throw "Character capture failed: $($taskCase.name)/$taskBackend" }
            }
            $taskMetrics = Get-Content $taskMetricsPath -Raw | ConvertFrom-Json
            if ($taskMetrics.renderer.backend -ne $taskBackend -or -not $taskMetrics.asset_preview.ready -or
                $taskMetrics.asset_preview.path -ne $taskObject -or $taskMetrics.asset_preview.vertex_count -le 0 -or
                $taskMetrics.asset_preview.triangle_count -le 0 -or $taskMetrics.asset_preview.animation_index -ne $taskCase.animation -or
                $taskMetrics.asset_preview.lighting_profile -ne $taskCase.lighting -or $taskMetrics.asset_preview.graphics_quality -ne 3) {
                throw "Character/backend/preview contract mismatch: $($taskCase.name)/$taskBackend"
            }
            $taskBitmap = [Drawing.Bitmap]::new($taskImage)
            try {
                if ($taskBitmap.Width -ne 1440 -or $taskBitmap.Height -ne 900) { throw 'Unexpected character capture dimensions.' }
                $taskCrop = $taskMatrix.crop
                $taskRegion = $taskBitmap.Clone([Drawing.Rectangle]::new($taskCrop.x, $taskCrop.y, $taskCrop.width, $taskCrop.height), $taskBitmap.PixelFormat)
                try { $taskRegion.Save((Join-Path $taskRun 'model.png'), [Drawing.Imaging.ImageFormat]::Png) } finally { $taskRegion.Dispose() }
            } finally { $taskBitmap.Dispose() }
            foreach ($taskGuard in $taskCase.contentGuards) {
                $taskContent += [pscustomobject]@{backend=$taskBackend; result=(Test-RenderParityImageContent -ImagePath (Join-Path $taskRun 'model.png') -Guard $taskGuard)}
            }
        }
        $taskPairs = @()
        foreach ($taskBackend in @($Backends | Where-Object { $_ -ne 'opengl' })) {
            $taskDiff = Compare-RenderParityImages -ReferencePath (Join-Path $taskCaseOutput 'opengl/model.png') `
                -CandidatePath (Join-Path $taskCaseOutput "$taskBackend/model.png") -PixelChannelTolerance $taskLimits.pixelChannelTolerance `
                -HeatmapScale $taskLimits.heatmapScale -HeatmapPath (Join-Path $taskCaseOutput "$taskBackend/difference.png")
            $taskPassed = $taskDiff.MeanAbsoluteError -le $taskLimits.meanAbsoluteError -and
                $taskDiff.RootMeanSquareError -le $taskLimits.rootMeanSquareError -and $taskDiff.ChangedPixelRatio -le $taskLimits.changedPixelRatio
            $taskPairs += [pscustomobject]@{backend=$taskBackend; passed=$taskPassed; metrics=$taskDiff}
        }
        $taskBaselinePairs = @()
        if ($BaselineDirectory) {
            foreach ($taskBackend in $Backends) {
                $taskBaseline = [IO.Path]::GetFullPath([IO.Path]::Combine($taskRoot, $BaselineDirectory, "$($taskCase.name)/$taskBackend/model.png"))
                $taskDiff = Compare-RenderParityImages -ReferencePath $taskBaseline `
                    -CandidatePath (Join-Path $taskCaseOutput "$taskBackend/model.png") -PixelChannelTolerance $taskLimits.pixelChannelTolerance `
                    -HeatmapScale $taskLimits.heatmapScale -HeatmapPath (Join-Path $taskCaseOutput "$taskBackend/baseline-difference.png")
                $taskPassed = $taskDiff.MeanAbsoluteError -le $taskLimits.meanAbsoluteError -and
                    $taskDiff.RootMeanSquareError -le $taskLimits.rootMeanSquareError -and $taskDiff.ChangedPixelRatio -le $taskLimits.changedPixelRatio
                $taskBaselinePairs += [pscustomobject]@{backend=$taskBackend; passed=$taskPassed; metrics=$taskDiff}
            }
        }
        $taskPassed = @($taskContent | Where-Object { -not $_.result.Passed }).Count -eq 0 -and @($taskPairs | Where-Object { -not $_.passed }).Count -eq 0
        $taskPassed = $taskPassed -and @($taskBaselinePairs | Where-Object { -not $_.passed }).Count -eq 0
        $taskResults += [pscustomobject]@{name=$taskCase.name; model=$taskCase.model; coverage=$taskCase.coverage; passed=$taskPassed; content=$taskContent; pairs=$taskPairs; baselinePairs=$taskBaselinePairs}
        Write-Host "[CharacterMaterials] $($taskCase.name): passed=$taskPassed"
        $taskResults | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $taskOutput 'report.json')
    }
} finally {
    foreach ($taskKey in $taskPrior.Keys) { [Environment]::SetEnvironmentVariable($taskKey, $taskPrior[$taskKey], 'Process') }
}
if (@($taskResults | Where-Object { -not $_.passed }).Count) { throw 'Character material parity/content qualification failed.' }
