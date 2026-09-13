[CmdletBinding()]
param(
    [ValidateSet('Debug','Release','RelWithDebInfo')][string[]]$Configurations = @('RelWithDebInfo'),
    [ValidateSet('opengl','vulkan','d3d12')][string[]]$Backends = @('opengl','vulkan','d3d12'),
    [string]$SceneId = 'routes/route1-flat-experiment',
    [string]$Scenario = 'route1-flat-experiment-crowded',
    [switch]$Play,
    [ValidateRange(300,100000)][int]$Frames = 1200,
    [ValidateRange(10,10000)][int]$WarmupSamples = 120,
    [string]$OutputDirectory = 'debug/editor-performance/benchmark'
)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskEngine = [IO.Path]::GetFullPath((Join-Path $taskRoot '../../Phlosion/PhlosionEngine'))
$taskOutput = [IO.Path]::GetFullPath([IO.Path]::Combine($taskRoot,$OutputDirectory))
if ($Frames - $WarmupSamples -lt 150) { throw 'Keep at least 150 scored frames after warmup.' }
# Competing renderers, compiler jobs or contract tests distort the comparison.
# MSBuild keeps idle reusable worker processes after a finished build.
if (Get-Process PhlosionEditor,PokemonAutochess,PAC_Tests,PhlosionEngineTests,cl,link -ErrorAction SilentlyContinue) {
    throw 'Close the editor/game and wait for builds and contract tests to finish before benchmarking.'
}
$taskProject = Get-Content (Join-Path $taskRoot 'phlosion.project.json') -Raw | ConvertFrom-Json
if ($SceneId -notin $taskProject.scenes.scene_id) { throw "Unknown scene: $SceneId" }
$taskProject.startup_scene.scene_id = $SceneId
$taskDescriptor = Join-Path $taskRoot ".phlosion.editor-benchmark.$PID.project.json"
$taskRows = @()
$taskPreviousCaptureEnvironment = @{}
foreach ($taskKey in @('PHLOSION_BACKEND_SCREENSHOT_PATH','PHLOSION_BACKEND_SCREENSHOT_FRAME','PHLOSION_BACKEND_SCREENSHOT_DEFER','PAC_RANDOM_SEED')) {
    $taskPreviousCaptureEnvironment[$taskKey] = [Environment]::GetEnvironmentVariable($taskKey,'Process')
}
New-Item -ItemType Directory -Force $taskOutput | Out-Null
try {
    $env:PHLOSION_BACKEND_SCREENSHOT_PATH = $null
    $env:PHLOSION_BACKEND_SCREENSHOT_FRAME = $null
    $env:PHLOSION_BACKEND_SCREENSHOT_DEFER = $null
    $env:PAC_RANDOM_SEED = '12345'
    $taskProject | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $taskDescriptor
    foreach ($taskConfiguration in $Configurations) {
        & (Join-Path $taskRoot 'tools/housekeeping/build_editor_pair.ps1') -Configuration $taskConfiguration -VerifyOnly `
            -OutputDirectory (Join-Path $taskOutput "pair-$taskConfiguration") | Out-Null
        foreach ($taskBackend in $Backends) {
            $taskRun = Join-Path $taskOutput "$taskConfiguration-$taskBackend"
            New-Item -ItemType Directory -Force $taskRun | Out-Null
            $taskArguments = @("--project=$taskDescriptor","--renderer=$taskBackend",'--hidden','--no-auto-reload','--stats',
                "--frames=$Frames",'--fixed-delta=0.016666667',"--metrics-warmup-samples=$WarmupSamples",
                "--state-directory=$taskRun/state","--metrics-output=$taskRun/metrics.json")
            if ($Scenario) { $taskArguments += "--game-preview=$Scenario" }
            if ($Play) { $taskArguments += '--play-game-preview' }
            # Paused setups compare a fixed roster; Play measures round/gameplay work too.
            # Screenshots belong to the separate visual gate, not the timed run.
            $taskProcess = Start-Process -FilePath (Join-Path $taskEngine "build/$taskConfiguration/PhlosionEditor.exe") `
                -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru `
                -ArgumentList ($taskArguments | ForEach-Object { '"'+$_+'"' }) `
                -RedirectStandardOutput (Join-Path $taskRun 'stdout.log') -RedirectStandardError (Join-Path $taskRun 'stderr.log')
            $null = $taskProcess.Handle
            $taskDeadline = [DateTime]::UtcNow.AddMinutes(10)
            while (-not $taskProcess.WaitForExit(1000)) {
                if ([DateTime]::UtcNow -gt $taskDeadline) {
                    Stop-Process -Id $taskProcess.Id
                    throw "Editor benchmark timed out: $taskConfiguration/$taskBackend"
                }
            }
            if ($taskProcess.ExitCode -ne 0) { throw "Editor benchmark failed: $taskConfiguration/$taskBackend" }
            $taskMetrics = Get-Content (Join-Path $taskRun 'metrics.json') -Raw | ConvertFrom-Json
            $taskCpu = $taskMetrics.renderer.cpu_frame_steady
            $taskGpu = $taskMetrics.renderer.gpu_frame_steady
            if ($taskMetrics.renderer.backend -ne $taskBackend -or $taskMetrics.project.active_scene.id -ne $SceneId -or
                $taskMetrics.capture.build_configuration -ne $taskConfiguration -or
                $taskMetrics.capture.metrics_warmup_samples -ne $WarmupSamples -or
                $taskCpu.sample_count -lt 150 -or $taskGpu.sample_count -lt 150 -or $taskCpu.mean_ms -le 0) {
                throw 'Wrong backend/build/scene, missing GPU timings, or insufficient steady samples.'
            }
            $taskUnitCount = @($taskMetrics.project.editor_contents.layout_objects | Where-Object {
                $_.id -like 'gameplay-preview/*' -and $_.viewport_visible }).Count
            if (-not $Play -and $Scenario -eq 'route1-flat-experiment-crowded' -and $taskUnitCount -ne 12) {
                throw 'The crowded benchmark must retain all twelve visible units.'
            }
            $taskRows += [pscustomobject]@{
                configuration=$taskConfiguration; backend=$taskBackend; scene=$SceneId; scenario=$Scenario; simulation=$(if ($Play) { 'playing' } else { 'paused' });
                viewport="$($taskMetrics.capture.viewport_width)x$($taskMetrics.capture.viewport_height)"; units=$taskUnitCount;
                frame_mean_ms=$taskCpu.mean_ms; frame_p95_ms=$taskCpu.p95_ms; gpu_mean_ms=$taskGpu.mean_ms;
                frame_max_ms=$taskCpu.max_ms;
                simulation_mean_ms=$taskMetrics.renderer.simulation_cpu_steady.mean_ms;
                simulation_max_ms=$taskMetrics.renderer.simulation_cpu_steady.max_ms;
                viewport_cpu_mean_ms=$taskMetrics.renderer.viewport_cpu_steady.mean_ms;
                present_mean_ms=$taskMetrics.renderer.present_wait_steady.mean_ms; samples=$taskCpu.sample_count
            }
            Write-Host "[EditorBenchmark] $taskConfiguration/$taskBackend frame=$([Math]::Round($taskCpu.mean_ms,2))ms GPU=$([Math]::Round($taskGpu.mean_ms,2))ms"
        }
    }
    $taskRows | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $taskOutput 'report.json')
    $taskRows | Export-Csv -NoTypeInformation (Join-Path $taskOutput 'report.csv')
    Write-Output (Join-Path $taskOutput 'report.json')
} finally {
    if (Test-Path -LiteralPath $taskDescriptor) { Remove-Item -LiteralPath $taskDescriptor }
    foreach ($taskKey in $taskPreviousCaptureEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($taskKey,$taskPreviousCaptureEnvironment[$taskKey],'Process')
    }
}
