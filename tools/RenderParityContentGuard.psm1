# Compatibility entry point; generic image analysis belongs to the engine.
Import-Module (Join-Path $PSScriptRoot '../../../Phlosion/PhlosionEngine/tools/render/RenderParityContentGuard.psm1') -Force
Export-ModuleMember -Function Test-RenderParityImageContent
