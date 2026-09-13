[CmdletBinding()]
param(
    [ValidateSet('Development','EngineDebug','Release')][string]$Mode = 'Development',
    [ValidateSet('auto','opengl','vulkan','d3d12')][string]$Backend = 'auto',
    [string]$Scenario = '',
    [string]$SceneId = '',
    [switch]$Play,
    [switch]$VerifyOnly
)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskEngine = [IO.Path]::GetFullPath((Join-Path $taskRoot '../../Phlosion/PhlosionEngine'))
$taskConfiguration = @{Development='RelWithDebInfo'; EngineDebug='Debug'; Release='Release'}[$Mode]
$taskProof = Join-Path $taskRoot 'debug/editor-launch/pair'
$taskPair = Join-Path $taskRoot 'tools/housekeeping/build_editor_pair.ps1'
if (-not $VerifyOnly -and (Get-Process PhlosionEditor -ErrorAction SilentlyContinue)) {
    throw 'An editor is already open. Use that window, or close it before launching another profile; concurrent builds can conflict with gameplay reload.'
}
if ($VerifyOnly) {
    & $taskPair -Configuration $taskConfiguration -VerifyOnly -OutputDirectory $taskProof | Out-Null
} else {
    try { & $taskPair -Configuration $taskConfiguration -VerifyOnly -OutputDirectory $taskProof | Out-Null }
    catch {
        Write-Host "Preparing the $Mode editor and matching gameplay module..."
        & $taskPair -Configuration $taskConfiguration -OutputDirectory $taskProof | Out-Null
    }
    & cmake --build (Join-Path $taskRoot 'build') --config $taskConfiguration --target PokemonAutochess --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Could not build the standalone game. Editor launch stopped.' }
}
$taskEditor = Join-Path $taskEngine "build/$taskConfiguration/PhlosionEditor.exe"
if ($Mode -eq 'Development') {
    foreach ($taskSymbols in @((Join-Path $taskEngine "build/$taskConfiguration/PhlosionEditor.pdb"),
                              (Join-Path $taskRoot ".phlosion/editor/$taskConfiguration/PokemonAutochessEditorProject.pdb"))) {
        if (-not (Test-Path -LiteralPath $taskSymbols)) { throw "Development debugging symbols are missing: $taskSymbols" }
    }
}
if ($VerifyOnly) { Write-Output "Verified $Mode ($taskConfiguration): $taskEditor"; return }
$taskProject = Join-Path $taskRoot 'phlosion.project.json'
if ($SceneId) {
    $taskDescriptor = Get-Content -LiteralPath $taskProject -Raw | ConvertFrom-Json
    if ($SceneId -notin $taskDescriptor.scenes.scene_id) { throw "Unknown scene: $SceneId" }
    $taskDescriptor.startup_scene.scene_id = $SceneId
    $taskProject = Join-Path $taskRoot '.phlosion.launch.project.json'
    $taskDescriptor | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $taskProject
}
$taskArguments = @("--project=$taskProject", "--renderer=$Backend", '--stats')
if ($Scenario) { $taskArguments += "--game-preview=$Scenario" }
if ($Play) { $taskArguments += '--play-game-preview' }
Write-Host "Opening Phlosion: $Mode. Stats and gameplay reload are available."
# This is the interactive editor the user explicitly opened.
Start-Process -FilePath $taskEditor -WorkingDirectory $taskRoot `
    -ArgumentList ($taskArguments | ForEach-Object { '"' + $_ + '"' })
