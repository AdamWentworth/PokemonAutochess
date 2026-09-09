[CmdletBinding()]
param(
    [string]$DepotRoot = $env:PHLOSION_ASSET_DEPOT,
    [string]$Destination = '',
    [string]$Recipe = 'config/environment/route1_south_entrance.authoring.json'
)
$ErrorActionPreference = 'Stop'
$taskGameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $DepotRoot) { throw 'Set PHLOSION_ASSET_DEPOT or supply -DepotRoot.' }
$taskArguments = @((Join-Path $PSScriptRoot 'restore_arena_source.py'), '--game-root', $taskGameRoot, '--recipe', $Recipe, '--depot', $DepotRoot)
if ($Destination) { $taskArguments += @('--destination', $Destination) }
& python @taskArguments
if ($LASTEXITCODE -ne 0) { throw 'Arena source restore failed.' }
