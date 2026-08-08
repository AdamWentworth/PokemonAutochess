[CmdletBinding()]
param(
    [string]$GameRoot = "",
    [string]$EngineRoot = "",
    [string]$OutputDirectory = "",
    [switch]$Fast
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $scriptRoot "..\.."
}
$GameRoot = [IO.Path]::GetFullPath($GameRoot)

if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
    $EngineRoot = Join-Path $GameRoot "..\..\Phlosion\PhlosionEngine"
}
$EngineRoot = [IO.Path]::GetFullPath($EngineRoot)

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmssZ')
    $OutputDirectory = Join-Path $GameRoot "artifacts\housekeeping\$stamp"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)

Import-Module (Join-Path $scriptRoot 'HousekeepingInventory.psm1') -Force

Write-Host "Inventorying Pokemon Autochess without modifying assets or builds..."
if ($Fast) {
    Write-Host "Fast mode: duplicate groups use declared hashes or size candidates."
} else {
    Write-Host "Full mode: verifying native and cooked duplicate content with SHA-256."
}

$verboseEnabled = $PSBoundParameters.ContainsKey('Verbose')
$inventory = New-HousekeepingInventory -GameRoot $GameRoot -EngineRoot $EngineRoot -Fast:$Fast -Verbose:$verboseEnabled
Assert-HousekeepingInventory -Inventory $inventory

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$jsonPath = Join-Path $OutputDirectory 'inventory.json'
$markdownPath = Join-Path $OutputDirectory 'inventory.md'
$hashPath = Join-Path $OutputDirectory 'inventory.sha256'

$inventory | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $jsonPath -Encoding UTF8
Write-HousekeepingMarkdownReport -Inventory $inventory -PathValue $markdownPath
$jsonHash = (Get-FileHash -LiteralPath $jsonPath -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath $hashPath -Value "$jsonHash *inventory.json" -Encoding ASCII

Write-Host "Inventory complete."
Write-Host "JSON: $jsonPath"
Write-Host "Report: $markdownPath"
Write-Host "Digest: $hashPath"
Write-Host "Active models: $($inventory.pokemon_config.unique_model_count)"
Write-Host "Cooked objects: $($inventory.cooked_objects.object_count)"
Write-Host "Findings: $($inventory.findings.Count)"

[pscustomobject]@{
    JsonPath = $jsonPath
    MarkdownPath = $markdownPath
    HashPath = $hashPath
    Inventory = $inventory
}
