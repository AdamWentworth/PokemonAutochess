[CmdletBinding()]
param([string[]]$Requested = @(), [string]$Platform = '')
$projectRoot = Split-Path $PSScriptRoot
& (Join-Path $projectRoot '../../Phlosion/PhlosionEngine/tools/Get-ProjectRenderers.ps1') `
    -Project (Join-Path $projectRoot 'phlosion.project.json') -Requested $Requested -Platform $Platform
