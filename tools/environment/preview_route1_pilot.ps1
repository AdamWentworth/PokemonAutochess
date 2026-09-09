[CmdletBinding()]
param(
    [ValidateSet('planning','battle')][string]$Phase = 'planning',
    [ValidateSet('opengl','d3d12','vulkan')][string]$Backend = 'opengl',
    [switch]$Capture
)
$ErrorActionPreference = 'Stop'
$taskGameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskProjects = [IO.Path]::GetFullPath((Join-Path $taskGameRoot '../..'))
$taskEditor = Join-Path $taskProjects 'Phlosion/PhlosionEngine/build/Release/PhlosionEditor.exe'
if (-not (Test-Path -LiteralPath $taskEditor)) { throw 'Build the editor pair with tools/housekeeping/build_editor_pair.ps1 first.' }
$taskOutput = Join-Path $taskGameRoot "debug/arena-pilot/editor-$Phase-$Backend"
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskProject = Join-Path $taskGameRoot '.phlosion.arena-pilot.project.json'
$taskDescriptor = Get-Content (Join-Path $taskGameRoot 'phlosion.project.json') -Raw | ConvertFrom-Json
$taskDescriptor.startup_scene.scene_id = 'routes/route1-pilot'
$taskDescriptor | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $taskProject
$taskArguments = @("--project=$taskProject", "--renderer=$Backend",
    "--game-preview=route1-pilot-$Phase", "--state-directory=$taskOutput/state")
if (-not $Capture) {
    # Explicit interactive preview launch.
    Start-Process -FilePath $taskEditor -WorkingDirectory $taskGameRoot -ArgumentList ($taskArguments | ForEach-Object { '"' + $_ + '"' })
    return
}
$taskPreviousPath = $env:PHLOSION_BACKEND_SCREENSHOT_PATH
$taskPreviousFrame = $env:PHLOSION_BACKEND_SCREENSHOT_FRAME
try {
    $env:PHLOSION_BACKEND_SCREENSHOT_PATH = Join-Path $taskOutput 'capture.png'
    $env:PHLOSION_BACKEND_SCREENSHOT_FRAME = '60'
    $taskArguments += @('--hidden', '--frames=65', '--fixed-delta=0.016666667', "--metrics-output=$taskOutput/metrics.json")
    $taskProcess = Start-Process -FilePath $taskEditor -WorkingDirectory $taskGameRoot -WindowStyle Hidden -PassThru `
        -ArgumentList ($taskArguments | ForEach-Object { '"' + $_ + '"' }) `
        -RedirectStandardOutput (Join-Path $taskOutput 'stdout.log') -RedirectStandardError (Join-Path $taskOutput 'stderr.log')
    if (-not $taskProcess.WaitForExit(240000)) { Stop-Process -Id $taskProcess.Id; throw 'Editor preview timed out.' }
    $taskProcess.WaitForExit()
    if ($taskProcess.ExitCode -ne 0) { throw "Editor preview exited with $($taskProcess.ExitCode)." }
    if (-not (Test-Path -LiteralPath $env:PHLOSION_BACKEND_SCREENSHOT_PATH)) { throw 'Editor screenshot was not produced.' }
    Write-Output $env:PHLOSION_BACKEND_SCREENSHOT_PATH
} finally {
    $env:PHLOSION_BACKEND_SCREENSHOT_PATH = $taskPreviousPath
    $env:PHLOSION_BACKEND_SCREENSHOT_FRAME = $taskPreviousFrame
}
