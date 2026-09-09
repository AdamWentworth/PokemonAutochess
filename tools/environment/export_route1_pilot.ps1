[CmdletBinding()]
param(
    [string]$BlendFile = '',
    [string]$Recipe = 'config/environment/route1_south_entrance.authoring.json',
    [string]$Blender = "$env:ProgramFiles/Blender Foundation/Blender 4.5/blender.exe",
    [switch]$OpenBlender,
    [switch]$UseExistingExport,
    [switch]$Capture
)
$ErrorActionPreference = 'Stop'
$taskGameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskRecipe = Get-Content ([IO.Path]::Combine($taskGameRoot, $Recipe)) -Raw | ConvertFrom-Json
if (-not $BlendFile) {
    if (-not $env:PHLOSION_ASSET_DEPOT) { throw 'Set PHLOSION_ASSET_DEPOT or supply -BlendFile.' }
    $BlendFile = Join-Path (Split-Path $env:PHLOSION_ASSET_DEPOT -Parent) $taskRecipe.working_source_relative
}
$BlendFile = [IO.Path]::GetFullPath($BlendFile)
$taskBridge = Join-Path $PSScriptRoot 'blender/arena_pilot.py'
$taskForge = Join-Path $taskGameRoot 'build/Release/PhlosionForge.exe'
foreach ($taskRequired in @($Blender, $BlendFile, $taskBridge, $taskForge)) {
    if (-not (Test-Path -LiteralPath $taskRequired)) { throw "Required file missing: $taskRequired" }
}
if ($OpenBlender) {
    # This is the explicitly requested visible authoring window.
    Start-Process -FilePath $Blender -WorkingDirectory $taskGameRoot -ArgumentList @(
        '--disable-autoexec', "`"$BlendFile`"", '--python', "`"$taskBridge`"", '--', 'ui', '--game-root', "`"$taskGameRoot`"", '--recipe', "`"$Recipe`"")
    return
}
$taskExport = Join-Path (Split-Path $BlendFile -Parent) 'export'
if (-not $UseExistingExport) {
    & $Blender --background --factory-startup --disable-autoexec $BlendFile --python-exit-code 1 --python $taskBridge -- export --output $taskExport --recipe $Recipe
    if ($LASTEXITCODE -ne 0) { throw 'Blender export failed; game files were not changed.' }
}
$taskArguments = @((Join-Path $PSScriptRoot 'publish_arena.py'), '--game-root', $taskGameRoot,
    '--recipe', $Recipe, '--export', $taskExport, '--blend', $BlendFile, '--forge', $taskForge)
if ($env:PHLOSION_ASSET_DEPOT) { $taskArguments += @('--depot', $env:PHLOSION_ASSET_DEPOT) }
& python @taskArguments
if ($LASTEXITCODE -ne 0) { throw 'Arena publication failed; inspect the export logs and active archive.' }
if ($Capture) { & (Join-Path $PSScriptRoot 'preview_route1_pilot.ps1') -Recipe $Recipe -Capture }
