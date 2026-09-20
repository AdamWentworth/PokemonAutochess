[CmdletBinding()]
param(
    [ValidateSet('d3d12', 'opengl', 'vulkan')][string]$Backend = 'opengl',
    [string]$OutputDirectory = 'debug/arena-pilot',
    [string]$Snapshot = 'config/debug/editor_route1_pilot_planning.json',
    [int]$Frame = 200,
    [ValidateRange(1, 16384)][int]$Width = 1440,
    [ValidateRange(1, 16384)][int]$Height = 1000,
    [ValidateSet('', '0', '1')][string]$VideoCharacterInking = '',
    [ValidateRange(0, 60)][int]$VideoSeconds = 0,
    [switch]$ShowGameWindow
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
if ($VideoSeconds -gt 0) {
    if (-not $ShowGameWindow) { throw 'Video recording requires -ShowGameWindow.' }
    $taskFfmpeg = (Get-Command ffmpeg -ErrorAction Stop).Source
    $taskFilterHelp = @(& $taskFfmpeg -hide_banner -h filter=gfxcapture 2>&1)
    if ($LASTEXITCODE -ne 0 -or -not ($taskFilterHelp -match '^Filter gfxcapture')) {
        throw 'Video recording requires a Windows FFmpeg build with the gfxcapture filter.'
    }
    if ($Width % 2 -ne 0 -or $Height % 2 -ne 0) { throw 'H.264 video dimensions must be even.' }
    $taskVariables.PAC_VIDEO_FPS_CAP = '60'
    $taskVariables.PAC_AUTO_QUIT_FRAMES = [string]($Frame + ($VideoSeconds + 3) * 60)
}
$taskPrevious = @{}
$taskProcess = $null
$taskRecorder = $null
try {
    foreach ($key in $taskVariables.Keys) {
        $taskPrevious[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
        [Environment]::SetEnvironmentVariable($key, $taskVariables[$key], 'Process')
    }
    $taskScreenshot = $taskVariables.PHLOSION_BACKEND_SCREENSHOT_PATH
    if (Test-Path -LiteralPath $taskScreenshot) { Remove-Item -LiteralPath $taskScreenshot }
    $taskWindowStyle = if ($ShowGameWindow) { 'Normal' } else { 'Hidden' }
    $taskProcess = Start-Process -FilePath (Join-Path $taskGameRoot 'build/Release/PokemonAutochess.exe') `
        -WindowStyle $taskWindowStyle -WorkingDirectory $taskGameRoot -PassThru `
        -RedirectStandardOutput (Join-Path $taskOutput "$Backend.stdout.log") `
        -RedirectStandardError (Join-Path $taskOutput "$Backend.stderr.log")
    # Windows PowerShell needs the process handle retained before waiting to
    # reliably expose ExitCode after Start-Process with redirected output.
    $null = $taskProcess.Handle
    if ($VideoSeconds -gt 0) {
        # Native readback establishes readiness before recording this process's window.
        $taskReadyTimer = [Diagnostics.Stopwatch]::StartNew()
        while (-not (Test-Path -LiteralPath $taskScreenshot)) {
            if ($taskProcess.HasExited -or $taskReadyTimer.Elapsed.TotalSeconds -gt 200) {
                throw 'The game did not reach the video start frame.'
            }
            Start-Sleep -Milliseconds 100
        }
        $taskProcess.Refresh()
        $taskWindow = $taskProcess.MainWindowHandle.ToInt64()
        if ($taskWindow -eq 0) { throw 'The visible game window is unavailable for video capture.' }
        $taskVideo = Join-Path $taskOutput "$Backend.mp4"
        # Windows Graphics Capture records the GPU surface even when another window overlaps it.
        $taskRecordArguments = @('-hide_banner', '-loglevel', 'warning', '-y',
            '-filter_complex', "gfxcapture=hwnd=${taskWindow}:capture_cursor=0:capture_border=0:max_framerate=30,hwdownload,format=bgra",
            '-t', [string]$VideoSeconds, '-an', '-c:v', 'libx264', '-preset', 'veryfast',
            '-crf', '18', '-pix_fmt', 'yuv420p', '-r', '30', '-fps_mode', 'cfr', '-movflags', '+faststart', $taskVideo)
        $taskRecorder = Start-Process -FilePath $taskFfmpeg -WindowStyle Hidden -PassThru `
            -ArgumentList ($taskRecordArguments | ForEach-Object { '"' + $_ + '"' }) `
            -RedirectStandardOutput (Join-Path $taskOutput 'ffmpeg.stdout.log') `
            -RedirectStandardError (Join-Path $taskOutput 'ffmpeg.stderr.log')
        $null = $taskRecorder.Handle
        if (-not $taskRecorder.WaitForExit(($VideoSeconds + 30) * 1000)) {
            throw 'Video recording timed out.'
        }
        $taskRecorder.WaitForExit()
        if ($taskRecorder.ExitCode -ne 0) {
            throw "FFmpeg exited with $($taskRecorder.ExitCode); see ffmpeg.stderr.log."
        }
        $taskSample = Join-Path $taskOutput 'video-check.png'
        & $taskFfmpeg -hide_banner -loglevel error -y -ss ([string]([Math]::Min(2, $VideoSeconds / 2))) `
            -i $taskVideo -frames:v 1 $taskSample
        if ($LASTEXITCODE -ne 0) { throw 'Could not inspect the recorded video.' }
        Import-Module (Join-Path $taskGameRoot 'tools/RenderParityContentGuard.psm1') -Force
        $taskVideoGuard = Test-RenderParityImageContent -ImagePath $taskSample -Guard ([pscustomobject]@{
            name='visible-recorded-game'; x=.3; y=.2; width=.4; height=.5;
            maximumNearBlackPixelRatio=.2; minimumMidtonePixelRatio=.2
        })
        if (-not $taskVideoGuard.Passed) { throw 'The video contains missing or black game content.' }
        Write-Output $taskVideo
    }
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
    foreach ($taskOwnedProcess in @($taskRecorder, $taskProcess)) {
        if ($taskOwnedProcess -and -not $taskOwnedProcess.HasExited) {
            Stop-Process -Id $taskOwnedProcess.Id
        }
    }
    foreach ($key in $taskPrevious.Keys) {
        [Environment]::SetEnvironmentVariable($key, $taskPrevious[$key], 'Process')
    }
}
