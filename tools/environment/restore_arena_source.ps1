[CmdletBinding()]
param(
    [string]$DepotRoot = $env:PHLOSION_ASSET_DEPOT,
    [string]$Destination = ''
)
$ErrorActionPreference = 'Stop'
$taskGameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskRecipe = Get-Content (Join-Path $taskGameRoot 'config/environment/route1_south_entrance.authoring.json') -Raw | ConvertFrom-Json
if (-not $DepotRoot) { throw 'Set PHLOSION_ASSET_DEPOT or supply -DepotRoot.' }
$DepotRoot = [IO.Path]::GetFullPath($DepotRoot)
$taskSource = Join-Path $DepotRoot $taskRecipe.depot_source_relative
if (-not $Destination) {
    $Destination = Join-Path (Split-Path $DepotRoot -Parent) $taskRecipe.working_source_relative
}
$Destination = [IO.Path]::GetFullPath($Destination)
if (-not (Test-Path -LiteralPath $taskSource -PathType Leaf)) { throw "Source backup is missing: $taskSource" }
if (Test-Path -LiteralPath $Destination) {
    throw "A working source already exists at $Destination. Choose a new -Destination to restore a separate copy."
}
New-Item -ItemType Directory -Path (Split-Path $Destination -Parent) -Force | Out-Null
Copy-Item -LiteralPath $taskSource -Destination $Destination
if ((Get-FileHash -LiteralPath $taskSource).Hash -ne (Get-FileHash -LiteralPath $Destination).Hash) {
    throw "Restored copy failed its hash check: $Destination"
}
Write-Output "Restored and verified: $Destination"
