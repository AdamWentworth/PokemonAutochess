[CmdletBinding()]
param(
    [ValidateSet('d3d12', 'opengl', 'vulkan')][string]$Backend = 'opengl',
    [string]$OutputDirectory = 'debug/arena-pilot',
    [string]$Snapshot = 'config/debug/editor_route1_pilot_planning.json',
    [int]$Frame = 200
)
$ErrorActionPreference = 'Stop'
$taskGameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskOutput = [IO.Path]::GetFullPath([IO.Path]::Combine($taskGameRoot, $OutputDirectory))
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskVariables = @{
    PHLOSION_DATA_ROOT = $taskGameRoot
    PHLOSION_ASSET_ROOT = (Join-Path $taskGameRoot 'assets')
    PAC_RENDER_BACKEND = $Backend
    PAC_VIDEO_WIDTH = '1440'; PAC_VIDEO_HEIGHT = '1000'; PAC_VIDEO_FULLSCREEN = '0'
    PAC_VIDEO_VSYNC = '0'; PAC_VIDEO_FPS_CAP = '0'; PAC_RANDOM_SEED = '12345'
    PAC_SHOW_PERF_OVERLAY = '0'
    PAC_FIXED_FRAME_DT_SECONDS = '0.016666667'
    PAC_DEBUG_STATE_PATH = $Snapshot
    PAC_AUTO_LOAD_DEBUG_SNAPSHOT = '1'; PAC_PIN_DEBUG_SNAPSHOT_STATE = '1'
    PAC_AUTO_QUIT_FRAMES = [string]($Frame + 5); PAC_AUTO_QUIT_SECONDS = '180'
    PHLOSION_BACKEND_SCREENSHOT_PATH = (Join-Path $taskOutput "$Backend.png")
    PHLOSION_BACKEND_SCREENSHOT_FRAME = [string]$Frame
}
$taskPrevious = @{}
try {
    foreach ($key in $taskVariables.Keys) {
        $taskPrevious[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
        [Environment]::SetEnvironmentVariable($key, $taskVariables[$key], 'Process')
    }
    $taskProcess = Start-Process -FilePath (Join-Path $taskGameRoot 'build/Release/PokemonAutochess.exe') `
        -WindowStyle Hidden -WorkingDirectory $taskGameRoot -PassThru `
        -RedirectStandardOutput (Join-Path $taskOutput "$Backend.stdout.log") `
        -RedirectStandardError (Join-Path $taskOutput "$Backend.stderr.log")
    if (-not $taskProcess.WaitForExit(240000)) {
        Stop-Process -Id $taskProcess.Id
        throw 'Arena capture timed out.'
    }
    $taskProcess.WaitForExit()
    if ($taskProcess.ExitCode -ne 0) { throw "Capture exited with $($taskProcess.ExitCode)." }
    if (-not (Test-Path -LiteralPath $taskVariables.PHLOSION_BACKEND_SCREENSHOT_PATH)) {
        throw 'The renderer did not produce the arena screenshot.'
    }
    Write-Output $taskVariables.PHLOSION_BACKEND_SCREENSHOT_PATH
} finally {
    foreach ($key in $taskPrevious.Keys) {
        [Environment]::SetEnvironmentVariable($key, $taskPrevious[$key], 'Process')
    }
}
