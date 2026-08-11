Set-StrictMode -Version Latest

function Resolve-CleanupPath {
    param([Parameter(Mandatory = $true)][string]$PathValue)
    return [IO.Path]::GetFullPath($PathValue).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
}

function Assert-CleanupTargetPath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string]$Target
    )

    if ([string]::IsNullOrWhiteSpace($RelativePath) -or
        [IO.Path]::IsPathRooted($RelativePath) -or
        $RelativePath.Contains('/') -or
        $RelativePath.Contains('\')) {
        throw "Cleanup target must be one allowlisted root child: $RelativePath"
    }
    $resolvedRoot = Resolve-CleanupPath $Root
    $resolvedTarget = Resolve-CleanupPath $Target
    $expectedTarget = Resolve-CleanupPath (Join-Path $resolvedRoot $RelativePath)
    if (-not $resolvedTarget.Equals(
            $expectedTarget,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Cleanup target does not match its allowlisted root child: $resolvedTarget"
    }
    $rootPrefix = $resolvedRoot + [IO.Path]::DirectorySeparatorChar
    if ($resolvedTarget.Equals(
            $resolvedRoot,
            [StringComparison]::OrdinalIgnoreCase) -or
        -not $resolvedTarget.StartsWith(
            $rootPrefix,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Cleanup target escapes its workspace root: $resolvedTarget"
    }
}

function Get-CleanupDirectoryStats {
    param([Parameter(Mandatory = $true)][string]$PathValue)

    if (-not (Test-Path -LiteralPath $PathValue)) {
        return [pscustomobject][ordered]@{
            exists = $false
            file_count = 0
            directory_count = 0
            bytes = [int64]0
            last_write_utc = $null
            reparse_points = @()
        }
    }
    if (-not (Test-Path -LiteralPath $PathValue -PathType Container)) {
        throw "Cleanup target exists but is not a directory: $PathValue"
    }
    $rootItem = Get-Item -LiteralPath $PathValue -Force
    if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Cleanup target root is a reparse point: $PathValue"
    }
    $children = @(Get-ChildItem -LiteralPath $PathValue -Force -Recurse)
    $reparsePoints = @(
        @($rootItem) + $children |
            Where-Object {
                ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
            } |
            ForEach-Object { [string]$_.FullName })
    $files = @($children | Where-Object { -not $_.PSIsContainer })
    $directories = @($children | Where-Object { $_.PSIsContainer })
    return [pscustomobject][ordered]@{
        exists = $true
        file_count = $files.Count
        directory_count = 1 + $directories.Count
        bytes = [int64](($files | Measure-Object -Property Length -Sum).Sum)
        last_write_utc = $rootItem.LastWriteTimeUtc.ToString('o')
        reparse_points = $reparsePoints
    }
}

function Get-CleanupTargetDefinitions {
    param(
        [Parameter(Mandatory = $true)][string]$GameRoot,
        [string]$EngineRoot
    )

    $definitions = @(
        [pscustomobject]@{ owner = 'game'; root = $GameRoot; relative_path = 'build-vs2022'; category = 'historical_build'; description = 'Historical Visual Studio build tree' },
        [pscustomobject]@{ owner = 'game'; root = $GameRoot; relative_path = 'build-ninja'; category = 'historical_build'; description = 'Historical Ninja build tree' },
        [pscustomobject]@{ owner = 'game'; root = $GameRoot; relative_path = 'build-fetch-deps'; category = 'historical_build'; description = 'Historical dependency-fetch build tree' },
        [pscustomobject]@{ owner = 'game'; root = $GameRoot; relative_path = 'build-fetch'; category = 'historical_build'; description = 'Historical fetch build tree' },
        [pscustomobject]@{ owner = 'game'; root = $GameRoot; relative_path = 'build-assetless'; category = 'historical_build'; description = 'Historical assetless build tree' },
        [pscustomobject]@{ owner = 'game'; root = $GameRoot; relative_path = 'debug'; category = 'historical_build'; description = 'Historical loose debug output' },
        [pscustomobject]@{ owner = 'game'; root = $GameRoot; relative_path = 'cache'; category = 'runtime_cache'; description = 'Regenerable game runtime cache' },
        [pscustomobject]@{ owner = 'game'; root = $GameRoot; relative_path = '.phlosion'; category = 'plugin_output'; description = 'Regenerable local editor plugin output' })
    if (-not [string]::IsNullOrWhiteSpace($EngineRoot)) {
        $definitions += [pscustomobject]@{
            owner = 'engine'
            root = $EngineRoot
            relative_path = 'cache'
            category = 'runtime_cache'
            description = 'Regenerable engine runtime cache'
        }
    }
    return $definitions
}

function New-WorkspaceCleanupPlan {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$GameRoot,
        [string]$EngineRoot,
        [ValidateSet('AllRegenerable', 'HistoricalBuilds', 'Caches')]
        [string]$Scope = 'AllRegenerable'
    )

    $resolvedGameRoot = Resolve-CleanupPath $GameRoot
    if (-not (Test-Path -LiteralPath $resolvedGameRoot -PathType Container)) {
        throw "Game workspace root does not exist: $resolvedGameRoot"
    }
    $resolvedEngineRoot = $null
    if (-not [string]::IsNullOrWhiteSpace($EngineRoot)) {
        $resolvedEngineRoot = Resolve-CleanupPath $EngineRoot
        if (-not (Test-Path -LiteralPath $resolvedEngineRoot -PathType Container)) {
            throw "Engine workspace root does not exist: $resolvedEngineRoot"
        }
    }

    $definitions = @(Get-CleanupTargetDefinitions `
        -GameRoot $resolvedGameRoot `
        -EngineRoot $resolvedEngineRoot)
    if ($Scope -eq 'HistoricalBuilds') {
        $definitions = @($definitions | Where-Object category -eq 'historical_build')
    } elseif ($Scope -eq 'Caches') {
        $definitions = @($definitions | Where-Object category -eq 'runtime_cache')
    }

    $targets = foreach ($definition in $definitions) {
        $target = Resolve-CleanupPath (
            Join-Path $definition.root $definition.relative_path)
        Assert-CleanupTargetPath `
            -Root $definition.root `
            -RelativePath $definition.relative_path `
            -Target $target
        $stats = Get-CleanupDirectoryStats $target
        [pscustomobject][ordered]@{
            owner = $definition.owner
            category = $definition.category
            relative_path = $definition.relative_path
            root = $definition.root
            target = $target
            description = $definition.description
            exists = $stats.exists
            file_count = $stats.file_count
            directory_count = $stats.directory_count
            bytes = $stats.bytes
            last_write_utc = $stats.last_write_utc
            reparse_points = @($stats.reparse_points)
        }
    }

    return [pscustomobject][ordered]@{
        schema = 'pokemon-autochess-cleanup-plan-v1'
        generated_at_utc = [DateTime]::UtcNow.ToString('o')
        scope = $Scope
        execution_requested = $false
        confirmation_provided = $false
        roots = [pscustomobject][ordered]@{
            game = $resolvedGameRoot
            engine = $resolvedEngineRoot
        }
        protected_paths = @(
            'build',
            'artifacts',
            'assets',
            'content',
            'config',
            'docs',
            'scenes',
            'src',
            'tests',
            'tools')
        target_count = @($targets).Count
        existing_target_count = @($targets | Where-Object exists).Count
        reclaimable_files = [int64](($targets.file_count | Measure-Object -Sum).Sum)
        reclaimable_directories = [int64](($targets.directory_count | Measure-Object -Sum).Sum)
        reclaimable_bytes = [int64](($targets.bytes | Measure-Object -Sum).Sum)
        targets = @($targets)
    }
}

function Assert-WorkspaceCleanupPlan {
    param([Parameter(Mandatory = $true)][object]$Plan)

    if ($Plan.schema -ne 'pokemon-autochess-cleanup-plan-v1') {
        throw "Unsupported cleanup plan schema: $($Plan.schema)"
    }
    if ([int]$Plan.target_count -ne @($Plan.targets).Count) {
        throw 'Cleanup target count does not match its record array.'
    }
    $bytes = [int64](($Plan.targets.bytes | Measure-Object -Sum).Sum)
    if ($bytes -ne [int64]$Plan.reclaimable_bytes) {
        throw 'Cleanup byte total does not match its target records.'
    }
    foreach ($target in @($Plan.targets)) {
        Assert-CleanupTargetPath `
            -Root ([string]$target.root) `
            -RelativePath ([string]$target.relative_path) `
            -Target ([string]$target.target)
        if (@($target.reparse_points).Count -gt 0) {
            throw "Cleanup target contains a reparse point and is not safe to remove: $($target.target)"
        }
    }
}

function Invoke-WorkspaceCleanupPlan {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Plan,
        [switch]$ConfirmDeletion,
        [switch]$TestFixture
    )

    Assert-WorkspaceCleanupPlan $Plan
    if (-not $ConfirmDeletion) {
        throw 'Deletion requires the explicit -ConfirmDeletion switch.'
    }
    if ($TestFixture) {
        $systemTemp = Resolve-CleanupPath ([IO.Path]::GetTempPath())
        foreach ($root in @($Plan.roots.game, $Plan.roots.engine)) {
            if ([string]::IsNullOrWhiteSpace([string]$root)) { continue }
            $resolvedRoot = Resolve-CleanupPath ([string]$root)
            if (-not $resolvedRoot.StartsWith(
                    $systemTemp + [IO.Path]::DirectorySeparatorChar,
                    [StringComparison]::OrdinalIgnoreCase)) {
                throw 'TestFixture may only bypass process checks under the system temporary directory.'
            }
        }
    } else {
        $activeProcesses = @(
            Get-Process -ErrorAction SilentlyContinue |
                Where-Object {
                    $_.ProcessName -in @(
                        'PokemonAutochess',
                        'PhlosionEditor',
                        'PhlosionForge',
                        'PAC_Tests')
                })
        if ($activeProcesses.Count -gt 0) {
            throw 'Cleanup refuses to run while Pokemon Autochess, Phlosion Editor, Forge, or tests are active.'
        }
    }

    foreach ($target in @($Plan.targets)) {
        $current = Get-CleanupDirectoryStats ([string]$target.target)
        if ([bool]$current.exists -ne [bool]$target.exists -or
            [int64]$current.file_count -ne [int64]$target.file_count -or
            [int64]$current.directory_count -ne [int64]$target.directory_count -or
            [int64]$current.bytes -ne [int64]$target.bytes) {
            throw "Cleanup plan is stale; regenerate it before deleting: $($target.target)"
        }
        if (@($current.reparse_points).Count -gt 0) {
            throw "Cleanup target now contains a reparse point: $($target.target)"
        }
    }

    $removed = @()
    foreach ($target in @($Plan.targets | Where-Object exists)) {
        Remove-Item -LiteralPath ([string]$target.target) -Recurse -Force
        if (Test-Path -LiteralPath ([string]$target.target)) {
            throw "Cleanup target still exists after removal: $($target.target)"
        }
        $removed += $target
    }
    return [pscustomobject][ordered]@{
        removed_target_count = $removed.Count
        removed_files = [int64](($removed.file_count | Measure-Object -Sum).Sum)
        removed_directories = [int64](($removed.directory_count | Measure-Object -Sum).Sum)
        removed_bytes = [int64](($removed.bytes | Measure-Object -Sum).Sum)
        targets = @($removed | ForEach-Object { $_.target })
    }
}

Export-ModuleMember -Function `
    New-WorkspaceCleanupPlan, `
    Assert-WorkspaceCleanupPlan, `
    Invoke-WorkspaceCleanupPlan
