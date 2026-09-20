# Hardware-adapter preflight shared by the qualification lanes that need a
# real GPU.
#
# tools/runtime_visual_smoke.ps1 and tools/qualify_content.ps1 both call this
# module before rendering so a software-only adapter (Microsoft Basic Render
# Driver) or an unavailable enumeration is reported as unqualified instead of
# running a render matrix into a wall-clock timeout.
#
# Windows PowerShell 5.1 compatible: no -AsHashtable, no null-coalescing
# operators, no multi-child Join-Path.

Set-StrictMode -Version Latest

$script:SoftwareAdapterPatterns = @(
    'Microsoft Basic Render Driver',
    'Microsoft Basic Display Adapter',
    'Microsoft Remote Display Adapter',
    'Microsoft Hyper-V Video'
)

# Passthrough accessor so callers and contract fixtures agree on the software
# adapter set without reaching into module scope.
function Get-PacSoftwareAdapterPattern {
    return @($script:SoftwareAdapterPatterns)
}

# Enumerates display adapters, or returns the caller-supplied set. Fixtures pass
# synthetic adapter names so the qualification gates can be exercised without a
# physical GPU.
function Get-PacVideoAdapterName {
    param([AllowNull()] [string[]] $Adapters = $null)

    if ($null -ne $Adapters) {
        return @($Adapters | ForEach-Object { [string]$_ })
    }
    try {
        return @(Get-CimInstance -ClassName Win32_VideoController -ErrorAction Stop |
            ForEach-Object { [string]$_.Name })
    } catch {
    }
    try {
        return @(Get-WmiObject -Class Win32_VideoController -ErrorAction Stop |
            ForEach-Object { [string]$_.Name })
    } catch {
    }
    return @()
}

function Test-PacSoftwareAdapter {
    param([Parameter(Mandatory = $true)] [string] $Name)

    foreach ($pattern in $script:SoftwareAdapterPatterns) {
        if ($Name -like ('*' + $pattern + '*')) { return $true }
    }
    return $false
}

function Get-PacHardwareAssessment {
    [CmdletBinding()]
    param([AllowNull()] [string[]] $Adapters = $null)

    $names = @(Get-PacVideoAdapterName -Adapters $Adapters)
    $hardware = New-Object 'System.Collections.Generic.List[string]'
    $problems = New-Object 'System.Collections.Generic.List[string]'

    if ($names.Count -eq 0) {
        $problems.Add('no video adapter could be enumerated; a render qualification needs a real GPU')
    } else {
        foreach ($name in $names) {
            if (Test-PacSoftwareAdapter -Name $name) { continue }
            $hardware.Add($name)
        }
        if ($hardware.Count -eq 0) {
            $problems.Add(('only software renderers were detected ({0}); a render qualification needs a real GPU' -f ($names -join ', ')))
        }
    }

    return [pscustomobject][ordered]@{
        schema = 'pac-hardware-assessment-v1'
        ok = ($problems.Count -eq 0)
        adapters = @($names)
        hardware_adapters = @($hardware.ToArray())
        problems = @($problems.ToArray())
    }
}

function Assert-PacHardwareQualified {
    [CmdletBinding()]
    param(
        [AllowNull()] [string[]] $Adapters = $null,
        [string] $Context = 'render qualification'
    )

    $assessment = Get-PacHardwareAssessment -Adapters $Adapters
    if (-not $assessment.ok) {
        throw (
            '{0} is unqualified:{1}{2}' -f
                $Context,
                [Environment]::NewLine,
                ($assessment.problems -join [Environment]::NewLine))
    }
    return $assessment
}

Export-ModuleMember -Function @(
    'Get-PacSoftwareAdapterPattern',
    'Get-PacVideoAdapterName',
    'Test-PacSoftwareAdapter',
    'Get-PacHardwareAssessment',
    'Assert-PacHardwareQualified'
)
