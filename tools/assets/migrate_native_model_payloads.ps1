[CmdletBinding()]
param(
    [string]$ModelsRoot = '',
    [switch]$Apply
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($ModelsRoot)) {
    $ModelsRoot = Join-Path $PSScriptRoot '..\..\assets\models'
}
$ModelsRoot = [IO.Path]::GetFullPath($ModelsRoot)

Import-Module (Join-Path $PSScriptRoot 'NativeModelPayloadStore.psm1') -Force

$plan = New-NativeModelPayloadMigrationPlan -ModelsRoot $ModelsRoot
Write-Host "Native model payload plan: $ModelsRoot"
Write-Host "  manifests: $($plan.manifest_count)"
Write-Host "  physical payloads: $($plan.physical_payload_count)"
Write-Host "  unique payloads: $($plan.unique_payload_count)"
Write-Host "  legacy manifests: $($plan.legacy_manifest_count)"
Write-Host "  reclaimable bytes: $($plan.reclaimable_bytes)"
Write-Host "  orphan store payloads: $($plan.orphan_payload_count)"

if (-not $Apply) {
    Write-Host 'Plan only; pass -Apply to publish shared payloads, rewrite manifests, and remove unreferenced legacy payloads.'
    return [pscustomobject][ordered]@{
        schema = $plan.schema
        models_root = $plan.models_root
        manifest_count = $plan.manifest_count
        physical_payload_count = $plan.physical_payload_count
        unique_payload_count = $plan.unique_payload_count
        duplicate_group_count = $plan.duplicate_group_count
        shared_reference_group_count = $plan.shared_reference_group_count
        legacy_manifest_count = $plan.legacy_manifest_count
        reclaimable_bytes = $plan.reclaimable_bytes
        orphan_payload_count = $plan.orphan_payload_count
    }
}

$result = Invoke-NativeModelPayloadMigration -Plan $plan -ConfirmMigration
Write-Host "Migration complete."
Write-Host "  rewritten manifests: $($result.rewritten_manifest_count)"
Write-Host "  final payloads: $($result.final_payload_count)"
Write-Host "  removed legacy bytes: $($result.removed_legacy_payload_bytes)"
return $result
