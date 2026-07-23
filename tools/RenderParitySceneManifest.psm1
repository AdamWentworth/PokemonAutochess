$ErrorActionPreference = "Stop"

function Resolve-RenderParityRepoPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Assert-UnitInterval {
    param(
        [string]$Name,
        [double]$Value
    )

    if ($Value -lt 0.0 -or $Value -gt 1.0) {
        throw "Render parity manifest '$Name' must be between 0 and 1."
    }
}

function Import-RenderParitySceneManifest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot
    )

    $manifest = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if ($manifest.version -ne 1) {
        throw "Unsupported render parity scene manifest version '$($manifest.version)'."
    }
    if ($null -eq $manifest.capture -or $null -eq $manifest.thresholds) {
        throw "Render parity scene manifest requires capture and thresholds objects."
    }

    $capture = $manifest.capture
    if ($capture.width -le 0 -or $capture.height -le 0 -or
        $capture.fixedFrameDtSeconds -le 0.0 -or
        $capture.autoQuitSeconds -le 0 -or
        $capture.waitTimeoutSeconds -le 0) {
        throw "Render parity manifest capture values must be positive."
    }

    $thresholds = $manifest.thresholds
    Assert-UnitInterval -Name "meanAbsoluteError" -Value $thresholds.meanAbsoluteError
    Assert-UnitInterval -Name "rootMeanSquareError" -Value $thresholds.rootMeanSquareError
    Assert-UnitInterval -Name "changedPixelRatio" -Value $thresholds.changedPixelRatio
    if ($thresholds.pixelChannelTolerance -lt 0 -or
        $thresholds.pixelChannelTolerance -gt 255 -or
        $thresholds.heatmapScale -lt 1 -or
        $thresholds.heatmapScale -gt 32) {
        throw "Render parity manifest pixel tolerance or heatmap scale is out of range."
    }

    $scenes = @($manifest.scenes)
    if ($scenes.Count -eq 0) {
        throw "Render parity scene manifest must define at least one scene."
    }

    $knownNames = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($scene in $scenes) {
        if ([string]::IsNullOrWhiteSpace($scene.name) -or
            $scene.name -notmatch '^[a-z0-9][a-z0-9-]*$') {
            throw "Render parity scene names must use lowercase letters, numbers, and hyphens."
        }
        if (-not $knownNames.Add([string]$scene.name)) {
            throw "Render parity scene name '$($scene.name)' is duplicated."
        }
        if ([string]::IsNullOrWhiteSpace($scene.focus) -or
            @($scene.coverage).Count -eq 0 -or
            $scene.screenshotFrame -le 0) {
            throw "Render parity scene '$($scene.name)' has incomplete focus, coverage, or frame metadata."
        }
        if ($null -ne $scene.autoQuitSeconds -and $scene.autoQuitSeconds -le 0) {
            throw "Render parity scene '$($scene.name)' autoQuitSeconds must be positive."
        }
        if ($null -ne $scene.snapshotPath -and
            -not [string]::IsNullOrWhiteSpace([string]$scene.snapshotPath)) {
            $snapshotAbs = Resolve-RenderParityRepoPath `
                -RepoRoot $RepoRoot `
                -Path ([string]$scene.snapshotPath)
            if (-not (Test-Path -LiteralPath $snapshotAbs)) {
                throw "Render parity scene '$($scene.name)' snapshot does not exist: $snapshotAbs"
            }
        }

        $contentGuards = @()
        if ($null -ne $scene.PSObject.Properties["contentGuards"] -and
            $null -ne $scene.contentGuards) {
            $contentGuards = @($scene.contentGuards)
        }
        if (@($scene.coverage) -contains "world" -and $contentGuards.Count -eq 0) {
            throw "Render parity world scene '$($scene.name)' must define at least one content guard."
        }

        $knownGuardNames = [Collections.Generic.HashSet[string]]::new(
            [StringComparer]::OrdinalIgnoreCase)
        foreach ($guard in $contentGuards) {
            if ([string]::IsNullOrWhiteSpace([string]$guard.name) -or
                $guard.name -notmatch '^[a-z0-9][a-z0-9-]*$') {
                throw "Render parity content guard names must use lowercase letters, numbers, and hyphens."
            }
            if (-not $knownGuardNames.Add([string]$guard.name)) {
                throw "Render parity scene '$($scene.name)' duplicates content guard '$($guard.name)'."
            }

            foreach ($requiredProperty in @(
                    "x",
                    "y",
                    "width",
                    "height",
                    "maximumNearBlackPixelRatio",
                    "minimumMidtonePixelRatio")) {
                if ($null -eq $guard.PSObject.Properties[$requiredProperty] -or
                    $null -eq $guard.$requiredProperty) {
                    throw "Render parity content guard '$($guard.name)' requires '$requiredProperty'."
                }
            }

            Assert-UnitInterval -Name "$($guard.name).x" -Value $guard.x
            Assert-UnitInterval -Name "$($guard.name).y" -Value $guard.y
            Assert-UnitInterval -Name "$($guard.name).width" -Value $guard.width
            Assert-UnitInterval -Name "$($guard.name).height" -Value $guard.height
            Assert-UnitInterval `
                -Name "$($guard.name).maximumNearBlackPixelRatio" `
                -Value $guard.maximumNearBlackPixelRatio
            Assert-UnitInterval `
                -Name "$($guard.name).minimumMidtonePixelRatio" `
                -Value $guard.minimumMidtonePixelRatio

            if ($guard.width -le 0.0 -or
                $guard.height -le 0.0 -or
                $guard.x + $guard.width -gt 1.0 -or
                $guard.y + $guard.height -gt 1.0) {
                throw "Render parity content guard '$($guard.name)' must be a positive normalized rectangle inside the image."
            }

            $nearBlackMaximum = 16
            $midtoneMinimum = 64
            $midtoneMaximum = 190
            if ($null -ne $guard.nearBlackLuminanceMaximum) {
                $nearBlackMaximum = [int]$guard.nearBlackLuminanceMaximum
            }
            if ($null -ne $guard.midtoneLuminanceMinimum) {
                $midtoneMinimum = [int]$guard.midtoneLuminanceMinimum
            }
            if ($null -ne $guard.midtoneLuminanceMaximum) {
                $midtoneMaximum = [int]$guard.midtoneLuminanceMaximum
            }
            if ($nearBlackMaximum -lt 0 -or
                $midtoneMaximum -gt 255 -or
                $nearBlackMaximum -ge $midtoneMinimum -or
                $midtoneMinimum -ge $midtoneMaximum) {
                throw "Render parity content guard '$($guard.name)' has invalid luminance thresholds."
            }
        }
    }

    return $manifest
}

function Select-RenderParityScenes {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Scenes,
        [string[]]$RequestedCases = @()
    )

    if ($RequestedCases.Count -eq 0) {
        return @($Scenes)
    }

    $requested = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($caseName in $RequestedCases) {
        if (-not [string]::IsNullOrWhiteSpace($caseName)) {
            [void]$requested.Add($caseName)
        }
    }

    $known = @($Scenes | ForEach-Object { [string]$_.name })
    $unknown = @($requested | Where-Object { $known -notcontains $_ })
    if ($unknown.Count -gt 0) {
        throw "Unknown render parity case(s): $($unknown -join ', '). Known cases: $($known -join ', ')."
    }

    $selected = @($Scenes | Where-Object { $requested.Contains([string]$_.name) })
    if ($selected.Count -eq 0) {
        throw "No render parity scenes were selected."
    }
    return $selected
}

Export-ModuleMember -Function `
    Import-RenderParitySceneManifest, `
    Resolve-RenderParityRepoPath, `
    Select-RenderParityScenes
