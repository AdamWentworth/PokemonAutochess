$ErrorActionPreference = "Stop"

function Get-RendererQualificationVulkanModeEvidence {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$MatrixOutputDirectory
    )

    $evidence = @()
    $logs = @(
        Get-ChildItem `
            -LiteralPath $MatrixOutputDirectory `
            -Filter "vulkan.stdout.log" `
            -File `
            -Recurse `
            -ErrorAction SilentlyContinue)
    foreach ($log in $logs) {
        $line = @(
            Get-Content -LiteralPath $log.FullName |
                Where-Object { $_ -match '^\[Vulkan\] Initialized ' } |
                Select-Object -Last 1)
        if ($line.Count -eq 0) {
            continue
        }
        $text = [string]$line[-1]
        if ($text -notmatch (
                "^\[Vulkan\] Initialized adapter='(?<adapter>[^']*)'.*" +
                "descriptorIndexing=(?<descriptor>[01]) " +
                "indirectWorld=(?<indirect>[01])")) {
            continue
        }
        $evidence += [pscustomobject]@{
            Scene = Split-Path -Leaf (Split-Path -Parent $log.FullName)
            LogPath = $log.FullName
            Adapter = $Matches["adapter"]
            DescriptorIndexing = [int]$Matches["descriptor"]
            IndirectWorld = [int]$Matches["indirect"]
            Line = $text
        }
    }
    return @($evidence | Sort-Object Scene)
}

Export-ModuleMember -Function Get-RendererQualificationVulkanModeEvidence
