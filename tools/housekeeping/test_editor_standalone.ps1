[CmdletBinding()]
param(
    [ValidateSet('RelWithDebInfo','Release','Debug')][string]$Configuration = 'RelWithDebInfo',
    [ValidateSet('opengl','vulkan','d3d12')][string]$Backend = 'd3d12',
    [string]$OutputDirectory = 'debug/editor-standalone'
)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskEngine = [IO.Path]::GetFullPath((Join-Path $taskRoot '../../Phlosion/PhlosionEngine'))
$taskOutput = [IO.Path]::GetFullPath((Join-Path $taskRoot $OutputDirectory))
if (Get-Process PhlosionEditor,PokemonAutochess -ErrorAction SilentlyContinue) {
    throw 'Close the editor and game before the isolated standalone-launch test.'
}
New-Item -ItemType Directory -Force $taskOutput | Out-Null
$taskDescriptorPath = Join-Path $taskRoot ".phlosion.standalone-test.$PID.project.json"
$taskScreenshot = Join-Path $taskOutput 'standalone.png'
$taskEditor = $null
$taskGame = $null
try {
    foreach ($taskFailBuild in @($true, $false)) {
        $taskCase = if ($taskFailBuild) { 'failed-build' } else { 'launch' }
        $taskDescriptor = Get-Content (Join-Path $taskRoot 'phlosion.project.json') -Raw | ConvertFrom-Json
        $taskPlay = $taskDescriptor.play_configurations | Where-Object id -eq 'standalone-game'
        if ($taskFailBuild) { $taskPlay.build_target = 'PhlosionIntentionalMissingTarget' }
        $taskPlay.environment = [ordered]@{
            PAC_RENDER_BACKEND='{renderer}'; PAC_AUTO_QUIT_FRAMES='166'; PAC_AUTO_QUIT_SECONDS='15';
            PAC_VIDEO_WIDTH='640'; PAC_VIDEO_HEIGHT='480'; PAC_VIDEO_FULLSCREEN='0'; PAC_VIDEO_VSYNC='0';
            PAC_RANDOM_SEED='12345'; PAC_FIXED_FRAME_DT_SECONDS='0.016666666666666666';
            PAC_DEBUG_STATE_PATH='config/debug/editor_route1_flat_experiment_benches.json';
            PAC_AUTO_LOAD_DEBUG_SNAPSHOT='1'; PAC_PIN_DEBUG_SNAPSHOT_STATE='1';
            PHLOSION_BACKEND_SCREENSHOT_PATH=$taskScreenshot; PHLOSION_BACKEND_SCREENSHOT_FRAME='164';
            PHLOSION_BACKEND_SCREENSHOT_DEFER='1'; PHLOSION_PARITY_CONTRACT_FATAL='1'
        }
        $taskDescriptor | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $taskDescriptorPath
        $taskArguments = @("--project=$taskDescriptorPath", "--renderer=$Backend", '--hidden', '--no-auto-reload',
            '--launch-play=standalone-game', '--exit-after-play-launch', "--state-directory=$taskOutput/$taskCase-state")
        $taskEditor = Start-Process -FilePath (Join-Path $taskEngine "build/$Configuration/PhlosionEditor.exe") `
            -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru `
            -ArgumentList ($taskArguments | ForEach-Object { '"'+$_+'"' }) `
            -RedirectStandardOutput (Join-Path $taskOutput "$taskCase.stdout.log") `
            -RedirectStandardError (Join-Path $taskOutput "$taskCase.stderr.log")
        $null = $taskEditor.Handle
        $taskDeadline = [DateTime]::UtcNow.AddMinutes(5)
        while (-not $taskEditor.WaitForExit(500)) {
            if ([DateTime]::UtcNow -gt $taskDeadline) { throw 'Standalone build/launch timed out.' }
        }
        Copy-Item -LiteralPath (Join-Path $taskRoot '.phlosion/standalone-build.log') -Destination (Join-Path $taskOutput "$taskCase.build.log")
        if ($taskFailBuild) {
            if ($taskEditor.ExitCode -eq 0 -or (Get-Process PokemonAutochess -ErrorAction SilentlyContinue)) {
                throw 'An unsuccessful build must fail the launch and never run a stale game.'
            }
            continue
        }
        if ($taskEditor.ExitCode -ne 0) { throw 'The successful incremental build did not launch standalone.' }
        $taskGame = Get-Process PokemonAutochess -ErrorAction Stop | Select-Object -First 1
        $null = $taskGame.Handle
        if ([IO.Path]::GetFullPath($taskGame.Path) -ne (Join-Path $taskRoot "build/$Configuration/PokemonAutochess.exe")) {
            throw 'Standalone launched the wrong build configuration.'
        }
        if (-not $taskGame.WaitForExit(180000)) { throw 'Standalone did not complete its rendered fixture.' }
        if ($taskGame.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $taskScreenshot)) {
            throw 'Standalone failed to render the requested fixture.'
        }
    }
    Import-Module (Join-Path $taskRoot 'tools/RenderParityContentGuard.psm1') -Force
    $taskGuard = Test-RenderParityImageContent -ImagePath $taskScreenshot -Guard ([pscustomobject]@{
        name='standalone-environment'; x=.3; y=.2; width=.4; height=.5;
        maximumNearBlackPixelRatio=.12; minimumMidtonePixelRatio=.3
    })
    if (-not $taskGuard.Passed) { throw 'Standalone output is missing the environment.' }
    @{passed=$true; configuration=$Configuration; renderer=$Backend; rejected_failed_build=$true; content=$taskGuard} |
        ConvertTo-Json -Depth 6 | Set-Content (Join-Path $taskOutput 'report.json')
    Write-Output "Standalone build, failure handling, launch and rendering passed: $taskOutput"
} finally {
    foreach ($taskProcess in @($taskEditor, $taskGame)) {
        if ($taskProcess -and -not $taskProcess.HasExited) { Stop-Process -Id $taskProcess.Id }
    }
    if (Test-Path -LiteralPath $taskDescriptorPath) { Remove-Item -LiteralPath $taskDescriptorPath }
}
