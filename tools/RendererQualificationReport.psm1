$ErrorActionPreference = "Stop"

function New-RendererQualificationStepResult {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [ValidateSet("Passed", "Failed", "Skipped")]
        [string]$Status,
        [Parameter(Mandatory = $true)]
        [DateTime]$StartedAtUtc,
        [Parameter(Mandatory = $true)]
        [DateTime]$FinishedAtUtc,
        [AllowNull()]
        [object]$Artifacts,
        [AllowNull()]
        [string]$ErrorMessage,
        [AllowNull()]
        [string]$SkipReason
    )

    if ([string]::IsNullOrWhiteSpace($Name)) {
        throw "Renderer qualification step name cannot be empty."
    }
    if ($FinishedAtUtc -lt $StartedAtUtc) {
        throw "Renderer qualification step '$Name' finished before it started."
    }
    if ($Status -eq "Failed" -and [string]::IsNullOrWhiteSpace($ErrorMessage)) {
        throw "Failed renderer qualification step '$Name' requires an error message."
    }
    if ($Status -eq "Skipped" -and [string]::IsNullOrWhiteSpace($SkipReason)) {
        throw "Skipped renderer qualification step '$Name' requires a reason."
    }

    return [pscustomobject]@{
        Name = $Name
        Status = $Status
        Passed = if ($Status -eq "Skipped") { $null } else { $Status -eq "Passed" }
        StartedAtUtc = $StartedAtUtc.ToString("o")
        FinishedAtUtc = $FinishedAtUtc.ToString("o")
        DurationSeconds = ($FinishedAtUtc - $StartedAtUtc).TotalSeconds
        Artifacts = $Artifacts
        ErrorMessage = $ErrorMessage
        SkipReason = $SkipReason
    }
}

function New-RendererQualificationReport {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [DateTime]$StartedAtUtc,
        [Parameter(Mandatory = $true)]
        [DateTime]$FinishedAtUtc,
        [Parameter(Mandatory = $true)]
        [object]$SystemInfo,
        [Parameter(Mandatory = $true)]
        [object]$Configuration,
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [object[]]$Steps
    )

    if ($FinishedAtUtc -lt $StartedAtUtc) {
        throw "Renderer qualification finished before it started."
    }

    $executedSteps = @($Steps | Where-Object { $_.Status -ne "Skipped" })
    $passedSteps = @($Steps | Where-Object { $_.Status -eq "Passed" })
    $failedSteps = @($Steps | Where-Object { $_.Status -eq "Failed" })
    $skippedSteps = @($Steps | Where-Object { $_.Status -eq "Skipped" })
    $passed = $executedSteps.Count -gt 0 -and $failedSteps.Count -eq 0

    return [pscustomobject]@{
        SchemaVersion = 1
        StartedAtUtc = $StartedAtUtc.ToString("o")
        FinishedAtUtc = $FinishedAtUtc.ToString("o")
        DurationSeconds = ($FinishedAtUtc - $StartedAtUtc).TotalSeconds
        System = $SystemInfo
        Configuration = $Configuration
        StepCount = $Steps.Count
        ExecutedStepCount = $executedSteps.Count
        PassedStepCount = $passedSteps.Count
        FailedStepCount = $failedSteps.Count
        SkippedStepCount = $skippedSteps.Count
        Passed = $passed
        Steps = @($Steps)
    }
}

function Write-RendererQualificationReport {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [object]$Report,
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $pathAbs = [IO.Path]::GetFullPath($Path)
    $parent = Split-Path -Parent $pathAbs
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $Report |
        ConvertTo-Json -Depth 12 |
        Set-Content -LiteralPath $pathAbs -Encoding UTF8
    return $pathAbs
}

function Get-RendererQualificationSystemInfo {
    [CmdletBinding()]
    param()

    $warnings = @()
    $operatingSystem = $null
    $processors = @()
    $displayAdapters = @()

    try {
        $os = Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop
        $operatingSystem = [pscustomobject]@{
            Caption = [string]$os.Caption
            Version = [string]$os.Version
            BuildNumber = [string]$os.BuildNumber
            OSArchitecture = [string]$os.OSArchitecture
        }
    } catch {
        $warnings += "Operating-system metadata unavailable: $($_.Exception.Message)"
    }

    try {
        $processors = @(
            Get-CimInstance -ClassName Win32_Processor -ErrorAction Stop |
                ForEach-Object {
                    [pscustomobject]@{
                        Name = [string]$_.Name
                        Manufacturer = [string]$_.Manufacturer
                        NumberOfCores = [int]$_.NumberOfCores
                        NumberOfLogicalProcessors = [int]$_.NumberOfLogicalProcessors
                    }
                })
    } catch {
        $warnings += "Processor metadata unavailable: $($_.Exception.Message)"
    }

    try {
        $displayAdapters = @(
            Get-CimInstance -ClassName Win32_VideoController -ErrorAction Stop |
                ForEach-Object {
                    [pscustomobject]@{
                        Name = [string]$_.Name
                        AdapterCompatibility = [string]$_.AdapterCompatibility
                        AdapterRamBytes = if ($null -eq $_.AdapterRAM) {
                            $null
                        } else {
                            [uint64]$_.AdapterRAM
                        }
                        DriverVersion = [string]$_.DriverVersion
                        DriverDate = if ($null -eq $_.DriverDate) {
                            $null
                        } else {
                            ([DateTime]$_.DriverDate).ToUniversalTime().ToString("o")
                        }
                        PnpDeviceId = [string]$_.PNPDeviceID
                        Status = [string]$_.Status
                        VideoProcessor = [string]$_.VideoProcessor
                    }
                })
    } catch {
        $warnings += "Display-adapter metadata unavailable: $($_.Exception.Message)"
    }

    return [pscustomobject]@{
        CapturedAtUtc = [DateTime]::UtcNow.ToString("o")
        ComputerName = [Environment]::MachineName
        OperatingSystem = $operatingSystem
        ProcessArchitecture = [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
        PowerShellVersion = $PSVersionTable.PSVersion.ToString()
        Processors = $processors
        DisplayAdapters = $displayAdapters
        MetadataWarnings = $warnings
    }
}

Export-ModuleMember -Function `
    Get-RendererQualificationSystemInfo, `
    New-RendererQualificationReport, `
    New-RendererQualificationStepResult, `
    Write-RendererQualificationReport
