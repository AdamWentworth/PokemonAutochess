[CmdletBinding()]
param(
    [ValidateSet('d3d12', 'opengl', 'vulkan')][string]$Backend = 'opengl',
    [string]$OutputDirectory = 'debug/arena-pilot',
    [string]$Snapshot = 'config/debug/editor_route1_pilot_planning.json',
    [int]$Frame = 200,
    [ValidateRange(1, 16384)][int]$Width = 1440,
    [ValidateRange(1, 16384)][int]$Height = 1000,
    [ValidateSet('', '0', '1')][string]$VideoCharacterInking = ''
)
$ErrorActionPreference = 'Stop'
$taskGameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskOutput = [IO.Path]::GetFullPath([IO.Path]::Combine($taskGameRoot, $OutputDirectory))
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskVariables = @{
    PHLOSION_DATA_ROOT = $taskGameRoot
    PHLOSION_ASSET_ROOT = (Join-Path $taskGameRoot 'assets')
    PAC_RENDER_BACKEND = $Backend
    PAC_VIDEO_WIDTH = [string]$Width; PAC_VIDEO_HEIGHT = [string]$Height; PAC_VIDEO_FULLSCREEN = '0'
    PAC_VIDEO_VSYNC = '0'; PAC_VIDEO_FPS_CAP = '0'; PAC_RANDOM_SEED = '12345'
    PAC_SHOW_PERF_OVERLAY = '0'
    PAC_FIXED_FRAME_DT_SECONDS = '0.016666667'
    PAC_DEBUG_STATE_PATH = $Snapshot
    PAC_AUTO_LOAD_DEBUG_SNAPSHOT = '1'; PAC_PIN_DEBUG_SNAPSHOT_STATE = '1'
    PAC_AUTO_QUIT_FRAMES = [string]($Frame + 5); PAC_AUTO_QUIT_SECONDS = '180'
    PHLOSION_BACKEND_SCREENSHOT_PATH = (Join-Path $taskOutput "$Backend.png")
    PHLOSION_BACKEND_SCREENSHOT_FRAME = [string]$Frame
    PHLOSION_BACKEND_SCREENSHOT_DEFER = '1'
}
if ($VideoCharacterInking -ne '') {
    $taskVariables.PAC_VIDEO_CHARACTER_INKING = $VideoCharacterInking
}
$taskPrevious = @{}
try {
    foreach ($key in $taskVariables.Keys) {
        $taskPrevious[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
        [Environment]::SetEnvironmentVariable($key, $taskVariables[$key], 'Process')
    }
    $taskScreenshot = $taskVariables.PHLOSION_BACKEND_SCREENSHOT_PATH
    if (Test-Path -LiteralPath $taskScreenshot) { Remove-Item -LiteralPath $taskScreenshot }
    $taskProcess = Start-Process -FilePath (Join-Path $taskGameRoot 'build/Release/PokemonAutochess.exe') `
        -WindowStyle Hidden -WorkingDirectory $taskGameRoot -PassThru `
        -RedirectStandardOutput (Join-Path $taskOutput "$Backend.stdout.log") `
        -RedirectStandardError (Join-Path $taskOutput "$Backend.stderr.log")
    # Windows PowerShell needs the process handle retained before waiting to
    # reliably expose ExitCode after Start-Process with redirected output.
    $null = $taskProcess.Handle
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
