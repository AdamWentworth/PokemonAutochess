[CmdletBinding()]
param(
    [string]$GameRoot = "",
    [string]$DepotRoot = $env:PHLOSION_ASSET_DEPOT,
    [string[]]$SourceTags = @('LGPE', 'PLA', 'SV', 'Sword', 'ZA'),
    [string[]]$ModelNames = @(),
    [ValidateRange(1, 8)]
    [int]$ThrottleLimit = 3,
    [switch]$NumberedOnly,
    [switch]$AllMatching,
    [switch]$PlanOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-FullPath([string]$PathValue) {
    return [IO.Path]::GetFullPath($PathValue)
}

function Assert-PathUnderRoot(
    [string]$PathValue,
    [string]$RootValue,
    [string]$Description) {
    $path = Resolve-FullPath $PathValue
    $root = (Resolve-FullPath $RootValue).TrimEnd('\', '/') +
        [IO.Path]::DirectorySeparatorChar
    if (-not $path.StartsWith(
            $root,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description escapes its allowed root: $path (root: $root)"
    }
    return $path
}

function Publish-ObjectDirectory(
    [string]$Source,
    [string]$Destination,
    [string]$AllowedRoot) {
    $sourcePath = Resolve-FullPath $Source
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) {
        throw "Cooked object directory is missing: $sourcePath"
    }
    $destinationPath = Assert-PathUnderRoot `
        $Destination `
        $AllowedRoot `
        'Published cooked object'
    New-Item -ItemType Directory -Path (Split-Path -Parent $destinationPath) -Force |
        Out-Null
    $partial = "$destinationPath.partial.$([Guid]::NewGuid().ToString('N'))"
    $backup = "$destinationPath.backup.$([Guid]::NewGuid().ToString('N'))"
    Copy-Item -LiteralPath $sourcePath -Destination $partial -Recurse -Force
    try {
        if (Test-Path -LiteralPath $destinationPath) {
            Move-Item -LiteralPath $destinationPath -Destination $backup
        }
        Move-Item -LiteralPath $partial -Destination $destinationPath
        if (Test-Path -LiteralPath $backup) {
            Assert-PathUnderRoot $backup $AllowedRoot 'Cooked object backup' |
                Out-Null
            Remove-Item -LiteralPath $backup -Recurse -Force
        }
    } catch {
        if ((-not (Test-Path -LiteralPath $destinationPath)) -and
            (Test-Path -LiteralPath $backup)) {
            Move-Item -LiteralPath $backup -Destination $destinationPath
        }
        throw
    } finally {
        if (Test-Path -LiteralPath $partial) {
            Assert-PathUnderRoot $partial $AllowedRoot 'Cooked object partial' |
                Out-Null
            Remove-Item -LiteralPath $partial -Recurse -Force
        }
    }
}

function Get-EyeUvChannelState([string]$ModelPath) {
    $document = Get-Content -LiteralPath $ModelPath -Raw |
        ConvertFrom-Json
    $eyeMaterials = @{}
    foreach ($material in @($document.materials)) {
        $name = [string]$material.name
        $family = [string]$material.shader_family
        if ($name.IndexOf(
                'eye',
                [StringComparison]::OrdinalIgnoreCase) -ge 0 -or
            $family -eq 'Eye' -or
            $family -eq 'EyeClearCoat') {
            $eyeMaterials[$name] = [pscustomobject]@{
                HasBase = $false
                HasNumbered = $false
            }
        }
    }
    foreach ($animation in @($document.animations)) {
        foreach ($track in @($animation.material_parameters)) {
            $materialName = [string]$track.material
            if (-not $eyeMaterials.ContainsKey($materialName)) { continue }
            $parameter = [string]$track.parameter
            if ($parameter -eq 'UVScaleOffset') {
                $eyeMaterials[$materialName].HasBase = $true
            } elseif ($parameter -match '^UVScaleOffset\d+$') {
                $eyeMaterials[$materialName].HasNumbered = $true
            }
        }
    }
    $hasAny = $false
    $requiresNumberedFallback = $false
    foreach ($state in $eyeMaterials.Values) {
        $hasAny = $hasAny -or $state.HasBase -or $state.HasNumbered
        $requiresNumberedFallback =
            $requiresNumberedFallback -or
            ($state.HasNumbered -and -not $state.HasBase)
    }
    return [pscustomobject]@{
        HasAny = $hasAny
        RequiresNumberedFallback = $requiresNumberedFallback
    }
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $scriptRoot '..\..'
}
$GameRoot = Resolve-FullPath $GameRoot
if ([string]::IsNullOrWhiteSpace($DepotRoot)) {
    $DepotRoot = 'D:\ProjectData\Games\PokemonAutochess\Assets'
}
$DepotRoot = Resolve-FullPath $DepotRoot

$modelsRoot = Assert-PathUnderRoot `
    (Join-Path $GameRoot 'assets\models') `
    $GameRoot `
    'Native model root'
$objectsRoot = Assert-PathUnderRoot `
    (Join-Path $GameRoot 'content\phlosion\objects') `
    $GameRoot `
    'Cooked object root'
$depotObjectsRoot = Join-Path `
    $DepotRoot `
    'pokemon-autochess\runtime\content\phlosion\objects'
$forge = Assert-PathUnderRoot `
    (Join-Path $GameRoot 'build\Debug\PhlosionForge.exe') `
    $GameRoot `
    'PhlosionForge executable'
foreach ($required in @($modelsRoot, $objectsRoot, $depotObjectsRoot, $forge)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required recook path is missing: $required"
    }
}

$tagPattern = ($SourceTags | ForEach-Object { [Regex]::Escape($_) }) -join '|'
$namePattern = "_(?:$tagPattern)(?:_|\.)"
$requestedModels = @{}
if ($NumberedOnly -and $AllMatching) {
    throw '-NumberedOnly and -AllMatching are mutually exclusive.'
}
foreach ($requestedModel in $ModelNames) {
    $normalizedName = [IO.Path]::GetFileName($requestedModel)
    if (-not $normalizedName.EndsWith(
            '.phmodel',
            [StringComparison]::OrdinalIgnoreCase)) {
        $normalizedName += '.phmodel'
    }
    $requestedModels[$normalizedName] = $true
}
$models = @(
    Get-ChildItem -LiteralPath $modelsRoot -Filter '*.phmodel' -File |
        Where-Object {
            if ($_.Name -notmatch $namePattern) { return $false }
            if ($requestedModels.Count -gt 0 -and
                -not $requestedModels.ContainsKey($_.Name)) {
                return $false
            }
            if ($AllMatching) { return $true }
            $channelState = Get-EyeUvChannelState $_.FullName
            if ($NumberedOnly) {
                return $channelState.RequiresNumberedFallback
            }
            return $channelState.HasAny
        } |
        Sort-Object Name
)
Write-Host (
    "Native-model recook plan: {0} models ({1}); throttle={2}; selection={3}" -f
        $models.Count,
        ($SourceTags -join ', '),
        $ThrottleLimit,
        $(if ($AllMatching) {
              'all-matching'
          } elseif ($NumberedOnly) {
              'numbered-only'
          } else {
              'base-or-numbered-eye-animation'
          }))
foreach ($model in $models) {
    Write-Host ("  " + $model.Name)
}
if ($PlanOnly) {
    Write-Host 'Plan validated; no objects were cooked or published.'
    exit 0
}
if ($models.Count -eq 0) {
    throw 'No native models matched the requested recook selection.'
}

$pending = New-Object 'System.Collections.Generic.Queue[object]'
foreach ($model in $models) { $pending.Enqueue($model) }
$running = New-Object System.Collections.ArrayList
$failures = New-Object System.Collections.Generic.List[string]
$completedCount = 0
while ($pending.Count -gt 0 -or $running.Count -gt 0) {
    while ($pending.Count -gt 0 -and $running.Count -lt $ThrottleLimit) {
        $model = $pending.Dequeue()
        $relativeModel = 'assets/models/' + $model.Name
        $job = Start-Job -ScriptBlock {
            param($ForgePath, $WorkingDirectory, $RelativeModel)
            Set-Location -LiteralPath $WorkingDirectory
            $lines = @(& $ForgePath cook-model $RelativeModel 2>&1)
            [pscustomobject]@{
                RelativeModel = $RelativeModel
                ExitCode = $LASTEXITCODE
                Lines = @($lines | ForEach-Object { [string]$_ })
            }
        } -ArgumentList $forge, $GameRoot, $relativeModel
        [void]$running.Add([pscustomobject]@{
            Job = $job
            Model = $model
        })
    }

    $finishedJob = Wait-Job -Job @($running | ForEach-Object { $_.Job }) -Any
    $entry = $running | Where-Object { $_.Job.Id -eq $finishedJob.Id } |
        Select-Object -First 1
    $result = Receive-Job -Job $finishedJob
    Remove-Job -Job $finishedJob
    [void]$running.Remove($entry)
    if ($null -eq $result -or [int]$result.ExitCode -ne 0) {
        $details = if ($null -eq $result) {
            'worker returned no result'
        } else {
            @($result.Lines) -join [Environment]::NewLine
        }
        $failures.Add($entry.Model.Name + ': ' + $details)
        Write-Warning ("Cook failed: " + $entry.Model.Name)
        continue
    }

    $objectNames = @(
        @(
            foreach ($line in @($result.Lines)) {
                if ([string]$line -match
                    'content[\\/]phlosion[\\/]objects[\\/]([^\\/]+)[\\/]') {
                    $Matches[1]
                }
            }
        ) | Select-Object -Unique
    )
    if ($objectNames.Count -ne 1 -or
        $objectNames[0] -notmatch '^[A-Za-z0-9_.-]+$') {
        $failures.Add(
            $entry.Model.Name + ': could not resolve one safe cooked object name')
        Write-Warning ("Cook output was ambiguous: " + $entry.Model.Name)
        continue
    }
    $objectName = [string]$objectNames[0]
    $sourceObject = Assert-PathUnderRoot `
        (Join-Path $objectsRoot $objectName) `
        $objectsRoot `
        'Cooked object'
    Publish-ObjectDirectory `
        $sourceObject `
        (Join-Path $depotObjectsRoot $objectName) `
        $depotObjectsRoot
    ++$completedCount
    Write-Host (
        "[{0}/{1}] cooked and published {2}" -f
            $completedCount,
            $models.Count,
            $entry.Model.Name)
}

if ($failures.Count -gt 0) {
    throw (
        "Native-model recook failed for {0} model(s):`n{1}" -f
            $failures.Count,
            ($failures -join "`n"))
}
Write-Host (
    "Cooked and published {0} native models." -f
        $completedCount)
