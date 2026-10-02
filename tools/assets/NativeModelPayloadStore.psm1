# Compatibility entry point; shared native model publication belongs to the adapter.
Import-Module (Join-Path $PSScriptRoot '../../../../Phlosion/PokemonModelSupport/tools/NativeModelPayloadStore.psm1') -Force
Export-ModuleMember -Function `
    Get-CanonicalNativePayloadRelativePath, `
    Get-NativeModelPayloadRecord, `
    Publish-NativeModelSharedPayload, `
    Remove-UnreferencedNativeModelPayloads, `
    New-NativeModelPayloadMigrationPlan, `
    Invoke-NativeModelPayloadMigration
