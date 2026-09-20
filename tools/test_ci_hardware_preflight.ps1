# Contract tests for the shared GPU/hardware preflight.
#
# The fixture supplies synthetic adapter names, so a software-only or
# unavailable enumeration is proven without capturing a real frame.

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot 'ci/HardwarePreflight.psm1') -Force

$checks = 0
function Assert-True {
    param([bool]$Condition, [string]$Message)
    $script:checks += 1
    if (-not $Condition) { throw $Message }
}
function Assert-Throws {
    param([scriptblock]$Action, [string]$Pattern, [string]$Message)
    $script:checks += 1
    $threw = $false
    try {
        & $Action | Out-Null
    } catch {
        $threw = $true
        if ($_.Exception.Message -notmatch $Pattern) {
            throw ("{0} (threw: {1})" -f $Message, $_.Exception.Message)
        }
    }
    if (-not $threw) { throw ("{0} (no exception was raised)" -f $Message) }
}

$hardware = Assert-PacHardwareQualified -Adapters @('NVIDIA GeForce RTX 4090') -Context 'fixture'
Assert-True ($hardware.ok) 'A discrete adapter must qualify.'
Assert-True (
    $hardware.hardware_adapters -contains 'NVIDIA GeForce RTX 4090') 'The assessment must record the qualifying adapter.'

$mixed = Get-PacHardwareAssessment -Adapters @('Microsoft Basic Render Driver', 'AMD Radeon RX 7900 XTX')
Assert-True ($mixed.ok) 'A machine with one hardware adapter must still qualify.'
Assert-True ($mixed.hardware_adapters.Count -eq 1) 'Software adapters must not be counted as hardware.'
Assert-True ($mixed.adapters.Count -eq 2) 'The assessment must keep the full adapter list for the report.'

$softwareOnly = { Assert-PacHardwareQualified -Adapters @('Microsoft Basic Render Driver') -Context 'fixture visual' }
Assert-Throws -Action $softwareOnly -Pattern 'unqualified' -Message 'A software-only adapter set must not qualify.'
$lowerCaseSoftware = { Assert-PacHardwareQualified -Adapters @('microsoft basic render driver') -Context 'fixture visual' }
Assert-Throws -Action $lowerCaseSoftware -Pattern 'unqualified' -Message 'Software adapter matching must ignore case.'
$unavailable = { Assert-PacHardwareQualified -Adapters @() -Context 'fixture visual' }
Assert-Throws -Action $unavailable -Pattern 'unqualified' -Message 'An unavailable adapter enumeration must not qualify.'

$empty = Get-PacHardwareAssessment -Adapters @()
Assert-True (-not $empty.ok) 'An empty adapter set must report not-ok.'
Assert-True (($empty.problems -join ' ') -match 'no video adapter') 'The unavailable case must explain the missing adapter.'

Assert-True ((Get-PacSoftwareAdapterPattern) -contains 'Microsoft Basic Render Driver') 'The software set must stay available.'
Assert-True (Test-PacSoftwareAdapter -Name 'Microsoft Hyper-V Video') 'Hyper-V video must count as software.'
Assert-True (-not (Test-PacSoftwareAdapter -Name 'Intel(R) Arc(TM) A770 Graphics')) 'A discrete Intel adapter is hardware.'

Write-Host ("[CiHardwarePreflightContractTest] PASS ({0} checks)" -f $checks)
