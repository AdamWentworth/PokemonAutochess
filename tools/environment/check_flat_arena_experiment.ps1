[CmdletBinding()]
param(
    [string]$OutputDirectory = 'debug/flat-experiment/qualified',
    [switch]$SkipCapture
)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskCases = @('flat-arena-crowded','flat-arena-earthquake','flat-arena-overlap-control','flat-arena-overlap',
               'original-arena-crowded','original-arena-earthquake','original-arena-overlap-control','original-arena-overlap')
Push-Location $taskRoot
try {
    & ./tools/render_parity_matrix.ps1 -Config Release -Cases $taskCases -OutputDir $OutputDirectory -SkipCapture:$SkipCapture
    $taskReport = Get-Content (Join-Path $OutputDirectory 'matrix-report.json') -Raw | ConvertFrom-Json
    if (-not $taskReport.Passed) { throw 'Arena comparison parity failed.' }
    Import-Module ./tools/RenderParityImageDiff.psm1 -Force
    $taskEvidence = @()
    foreach ($taskBackend in @('opengl','vulkan','d3d12')) {
        foreach ($taskCase in $taskCases) {
            $taskLog = Join-Path $OutputDirectory "$taskCase/$taskBackend.stdout.log"
            if (-not (Select-String -LiteralPath $taskLog -SimpleMatch "[CaptureTimeline] backend=$taskBackend origin=gameplay frame=0")) {
                throw "Missing native gameplay timeline: $taskCase/$taskBackend"
            }
        }
        foreach ($taskTerrain in @('flat','original')) {
            foreach ($taskPair in @(@('crowded','earthquake'),@('overlap-control','overlap'))) {
                $taskBaseline = Join-Path $OutputDirectory "$taskTerrain-arena-$($taskPair[0])/$taskBackend.png"
                $taskEffect = Join-Path $OutputDirectory "$taskTerrain-arena-$($taskPair[1])/$taskBackend.png"
                $taskDiff = Compare-RenderParityImages -ReferencePath $taskBaseline -CandidatePath $taskEffect
                # Same units, seed, simulation frame and scene. At least 4% of
                # the image must visibly change, preventing an all-API omission
                # from passing simply because three empty effects look alike.
                $taskPassed = $taskDiff.ChangedPixelRatio -ge .04 -and $taskDiff.MeanAbsoluteError -ge .01
                $taskEvidence += [pscustomobject]@{ terrain=$taskTerrain; phase=$taskPair[1]; backend=$taskBackend; passed=$taskPassed; metrics=$taskDiff }
            }
        }
    }
    $taskEvidence | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $OutputDirectory 'effect-presence.json')
    if (@($taskEvidence | Where-Object { -not $_.passed }).Count -gt 0) { throw 'Earthquake did not visibly change a matched control image.' }

    # These fixture pixels cover the layered effect and unobstructed dirt,
    # away from HUD overlays. A completed view must be opaque, even where its
    # materials used alpha blending. Previously Vulkan/D3D12 stored 59/126 at
    # the effect pixel and the editor composited the scene a second time.
    Add-Type -AssemblyName System.Drawing
    $taskOpacityChecks = @()
    foreach ($taskBackend in @('opengl','vulkan','d3d12')) {
        $taskImagePath = [IO.Path]::GetFullPath((Join-Path $OutputDirectory "flat-arena-earthquake/$taskBackend.png"))
        $taskBitmap = [Drawing.Bitmap]::FromFile($taskImagePath)
        try {
            $taskEffectAlpha = $taskBitmap.GetPixel(720,500).A
            $taskFloorAlpha = $taskBitmap.GetPixel(600,680).A
            $taskOpacityChecks += [pscustomobject]@{ backend=$taskBackend; effectAlpha=$taskEffectAlpha; floorAlpha=$taskFloorAlpha; passed=($taskEffectAlpha -eq 255 -and $taskFloorAlpha -eq 255) }
        } finally { $taskBitmap.Dispose() }
    }
    $taskOpacityChecks | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'completed-view-opacity.json')
    if (@($taskOpacityChecks | Where-Object { -not $_.passed }).Count -gt 0) { throw 'Completed world view contains unintended transparency.' }

    $taskEditorChecks = @()
    foreach ($taskTerrain in @('flat','original')) {
        $taskRecipe = if ($taskTerrain -eq 'flat') { 'config/environment/route1_flat_experiment.authoring.json' }
                      else { 'config/environment/route1_south_entrance.authoring.json' }
        foreach ($taskBackend in @('opengl','vulkan','d3d12')) {
            $taskEditorPath = Join-Path $OutputDirectory "editor/$taskTerrain/$taskBackend"
            if (-not $SkipCapture) {
                & ./tools/environment/preview_route1_pilot.ps1 -Recipe $taskRecipe -Phase earthquake -Play -Capture -Frame 164 -Backend $taskBackend -OutputDirectory $taskEditorPath
            }
            $taskEditorLog = Get-Content (Join-Path $taskEditorPath 'stdout.log') -Raw
            $taskEditorMetrics = Get-Content (Join-Path $taskEditorPath 'metrics.json') -Raw | ConvertFrom-Json
            if ($taskEditorMetrics.renderer.backend -ne $taskBackend -or
                $taskEditorLog -notmatch '\[ArenaEffectExperiment\] loaded meshes=77 cards=312' -or
                $taskEditorLog -notmatch '\[Init\] RNG seed: 12345') {
                throw "Editor comparison has the wrong backend, content or seed: $taskTerrain/$taskBackend"
            }
        }
        foreach ($taskBackend in @('vulkan','d3d12')) {
            $taskDiff = Compare-RenderParityImages -ReferencePath (Join-Path $OutputDirectory "editor/$taskTerrain/opengl/capture.png") -CandidatePath (Join-Path $OutputDirectory "editor/$taskTerrain/$taskBackend/capture.png")
            $taskLimits = $taskReport.Thresholds
            $taskPassed = $taskDiff.MeanAbsoluteError -le $taskLimits.meanAbsoluteError -and
                          $taskDiff.RootMeanSquareError -le $taskLimits.rootMeanSquareError -and
                          $taskDiff.ChangedPixelRatio -le $taskLimits.changedPixelRatio
            $taskEditorChecks += [pscustomobject]@{ terrain=$taskTerrain; pair="opengl-$taskBackend"; passed=$taskPassed; metrics=$taskDiff }
        }
    }
    $taskEditorChecks | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $OutputDirectory 'editor-parity.json')
    if (@($taskEditorChecks | Where-Object { -not $_.passed }).Count -gt 0) { throw 'Embedded editor comparison failed parity.' }
    Write-Host 'Game and editor match on all three APIs, with visible effects against every matched control.'
} finally { Pop-Location }
