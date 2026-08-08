[CmdletBinding()]
param(
    [string]$ModelsRoot = '',
    [int64]$DuplicateByteBudget = 0,
    [int]$LegacyManifestBudget = 0,
    [int]$OrphanPayloadBudget = 0
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($ModelsRoot)) {
    $ModelsRoot = Join-Path $PSScriptRoot '..\..\assets\models'
}
$ModelsRoot = [IO.Path]::GetFullPath($ModelsRoot)

Import-Module (Join-Path $PSScriptRoot 'NativeModelPayloadStore.psm1') -Force
$plan = New-NativeModelPayloadMigrationPlan -ModelsRoot $ModelsRoot

if ([int64]$plan.reclaimable_bytes -gt $DuplicateByteBudget) {
    throw "Native payload duplicate-byte budget exceeded: $($plan.reclaimable_bytes) > $DuplicateByteBudget"
}
if ([int]$plan.legacy_manifest_count -gt $LegacyManifestBudget) {
    throw "Native payload legacy-manifest budget exceeded: $($plan.legacy_manifest_count) > $LegacyManifestBudget"
}
if ([int]$plan.orphan_payload_count -gt $OrphanPayloadBudget) {
    throw "Native payload orphan budget exceeded: $($plan.orphan_payload_count) > $OrphanPayloadBudget"
}

Write-Host ("Native payload validation passed: {0} manifests, {1} shared payloads, {2} duplicate bytes." -f `
    $plan.manifest_count, $plan.physical_payload_count, $plan.reclaimable_bytes)
return [pscustomobject][ordered]@{
    schema = 'phlosion-native-payload-validation-v1'
    models_root = $plan.models_root
    manifest_count = $plan.manifest_count
    physical_payload_count = $plan.physical_payload_count
    unique_payload_count = $plan.unique_payload_count
    duplicate_group_count = $plan.duplicate_group_count
    shared_reference_group_count = $plan.shared_reference_group_count
    legacy_manifest_count = $plan.legacy_manifest_count
    duplicate_bytes = $plan.reclaimable_bytes
    orphan_payload_count = $plan.orphan_payload_count
}
