[CmdletBinding()]
param(
    [ValidateSet('planning','battle','ledges')][string]$Phase = 'planning',
    [ValidateSet('opengl','d3d12','vulkan')][string]$Backend = 'opengl',
    [string]$OutputDirectory = '',
    [string]$Recipe = 'config/environment/route1_south_entrance.authoring.json',
    [switch]$Capture
)
$ErrorActionPreference = 'Stop'
$taskGameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskRecipe = Get-Content ([IO.Path]::Combine($taskGameRoot, $Recipe)) -Raw | ConvertFrom-Json
$taskSceneId = $taskRecipe.scene_id
$taskPreviewPrefix = ($taskSceneId -split '/')[-1]
if ($Phase -eq 'ledges' -and $taskPreviewPrefix -ne 'route1-pilot') {
    throw 'The dedicated Ledge Test scenario currently belongs to South Entrance. Use Planning or Battle for this arena.'
}
$taskProjects = [IO.Path]::GetFullPath((Join-Path $taskGameRoot '../..'))
$taskEditor = Join-Path $taskProjects 'Phlosion/PhlosionEngine/build/Release/PhlosionEditor.exe'
if (-not (Test-Path -LiteralPath $taskEditor)) { throw 'Build the editor pair with tools/housekeeping/build_editor_pair.ps1 first.' }
$taskOutput = if ($OutputDirectory) { [IO.Path]::GetFullPath([IO.Path]::Combine($taskGameRoot, $OutputDirectory)) }
              else { Join-Path $taskGameRoot "debug/$taskPreviewPrefix/editor-$Phase-$Backend" }
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskProject = Join-Path $taskGameRoot ".phlosion.$taskPreviewPrefix.project.json"
$taskDescriptor = Get-Content (Join-Path $taskGameRoot 'phlosion.project.json') -Raw | ConvertFrom-Json
$taskDescriptor.startup_scene.scene_id = $taskSceneId
$taskDescriptor | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $taskProject
$taskArguments = @("--project=$taskProject", "--renderer=$Backend",
    "--game-preview=$taskPreviewPrefix-$Phase", "--state-directory=$taskOutput/state")
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
    if (Test-Path -LiteralPath $env:PHLOSION_BACKEND_SCREENSHOT_PATH) {
        Remove-Item -LiteralPath $env:PHLOSION_BACKEND_SCREENSHOT_PATH
    }
    $taskArguments += @('--hidden', '--frames=65', '--fixed-delta=0.016666667', "--metrics-output=$taskOutput/metrics.json")
    $taskProcess = Start-Process -FilePath $taskEditor -WorkingDirectory $taskGameRoot -WindowStyle Hidden -PassThru `
        -ArgumentList ($taskArguments | ForEach-Object { '"' + $_ + '"' }) `
        -RedirectStandardOutput (Join-Path $taskOutput 'stdout.log') -RedirectStandardError (Join-Path $taskOutput 'stderr.log')
    $null = $taskProcess.Handle
    if (-not $taskProcess.WaitForExit(240000)) { Stop-Process -Id $taskProcess.Id; throw 'Editor preview timed out.' }
    $taskProcess.WaitForExit()
    if ($taskProcess.ExitCode -ne 0) { throw "Editor preview exited with $($taskProcess.ExitCode)." }
    if (-not (Test-Path -LiteralPath $env:PHLOSION_BACKEND_SCREENSHOT_PATH)) { throw 'Editor screenshot was not produced.' }
    $taskMetrics = Get-Content (Join-Path $taskOutput 'metrics.json') -Raw | ConvertFrom-Json
    if ($taskMetrics.project.active_scene.id -ne $taskSceneId -or
        $taskMetrics.project.visible_triangles -le 0 -or $taskMetrics.renderer.backend -ne $Backend -or
        -not $taskMetrics.capture.hidden) { throw 'Editor preview did not qualify the requested arena/backend.' }
    Write-Output $env:PHLOSION_BACKEND_SCREENSHOT_PATH
} finally {
    $env:PHLOSION_BACKEND_SCREENSHOT_PATH = $taskPreviousPath
    $env:PHLOSION_BACKEND_SCREENSHOT_FRAME = $taskPreviousFrame
}
