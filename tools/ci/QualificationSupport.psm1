# Qualification report support for tools/qualify_content.ps1.
#
# The content qualification entrypoint must record what was actually tested:
# the game revision, the dirty state of the code, the dependency revisions the
# build resolved (from the CMake cache or a FetchContent download), and the
# strictly scoped runtime environment variables used for the render children.
#
# Windows PowerShell 5.1 compatible: no -AsHashtable, no null-coalescing
# operators, no multi-child Join-Path.

Set-StrictMode -Version Latest

$script:DependencyIdentitySchema = 'pac-build-dependency-identity-v1'

# Runtime strictness used by the visual/editor children. Source-shaped importer
# tests keep their own semantics; only the render/game process tree receives
# these values.
$script:StrictCookedEnvironment = [ordered]@{
    PHLOSION_REQUIRE_COOKED_ASSETS = '1'
    PHLOSION_TRACE_ASSET_LOADS = '1'
}

function Resolve-PacQualificationPath {
    param(
        [Parameter(Mandatory = $true)] [string] $RepoRoot,
        [Parameter(Mandatory = $true)] [string] $PathValue
    )

    if ([IO.Path]::IsPathRooted($PathValue)) {
        return [IO.Path]::GetFullPath($PathValue)
    }
    return [IO.Path]::GetFullPath((Join-Path $RepoRoot $PathValue))
}

function Get-PacGitCommandResult {
    param(
        [Parameter(Mandatory = $true)] [string] $RepositoryRoot,
        [Parameter(Mandatory = $true)] [string[]] $Arguments
    )

    try {
        $lines = @(& git -C $RepositoryRoot @Arguments 2>$null)
        $exitCode = $LASTEXITCODE
    } catch {
        return [pscustomobject][ordered]@{ exit_code = -1; lines = @() }
    }
    if ($null -eq $exitCode) { $exitCode = -1 }
    return [pscustomobject][ordered]@{ exit_code = [int]$exitCode; lines = @($lines | ForEach-Object { [string]$_ }) }
}

function Get-PacGitRevision {
    param([Parameter(Mandatory = $true)] [string] $RepositoryRoot)

    $result = Get-PacGitCommandResult -RepositoryRoot $RepositoryRoot -Arguments @('rev-parse', 'HEAD')
    if ($result.exit_code -ne 0 -or $result.lines.Count -eq 0) { return $null }
    $revision = ([string]$result.lines[0]).Trim()
    if ($revision.Length -ne 40) { return $null }
    return $revision
}

function Get-PacGitDirtyState {
    param([Parameter(Mandatory = $true)] [string] $RepositoryRoot)

    $result = Get-PacGitCommandResult -RepositoryRoot $RepositoryRoot -Arguments @('status', '--porcelain')
    if ($result.exit_code -ne 0) {
        return [pscustomobject][ordered]@{
            state = 'unknown'
            dirty = $true
            changed_paths = @()
        }
    }
    $paths = @($result.lines | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    $state = 'clean'
    if ($paths.Count -gt 0) { $state = 'dirty' }
    return [pscustomobject][ordered]@{
        state = $state
        dirty = ($paths.Count -gt 0)
        changed_paths = @($paths)
    }
}

# Reads a cached CMake variable. CMakeCache entries look like
# NAME:TYPE=VALUE and the value may itself contain '='.
function Get-PacCMakeCacheValue {
    param(
        [Parameter(Mandatory = $true)] [string] $BuildDir,
        [Parameter(Mandatory = $true)] [string] $Name
    )

    $cachePath = Join-Path $BuildDir 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) { return $null }
    $pattern = '^' + [regex]::Escape($Name) + ':[^=]*=(.*)$'
    $match = Select-String -LiteralPath $cachePath -Pattern $pattern | Select-Object -First 1
    if ($null -eq $match) { return $null }
    return [string]$match.Matches[0].Groups[1].Value
}

function Get-PacDependencyPin {
    param([Parameter(Mandatory = $true)] [string] $CMakeListsPath)

    if (-not (Test-Path -LiteralPath $CMakeListsPath -PathType Leaf)) { return $null }
    $text = [IO.File]::ReadAllText($CMakeListsPath)
    $pins = [ordered]@{}
    $definitions = @(
        @{ Name = 'PhlosionEngine'; Variable = 'PHLOSION_ENGINE_GIT_TAG' },
        @{ Name = 'PhlosionVFX'; Variable = 'PHLOSION_VFX_GIT_TAG' },
        @{ Name = 'PhlosionPackages'; Variable = 'PHLOSION_PACKAGES_GIT_TAG' }
    )
    foreach ($definition in $definitions) {
        $match = [regex]::Match(
            $text,
            ('set\(\s*' + [regex]::Escape($definition.Variable) + '\s+"([0-9a-f]{40})"'))
        if ($match.Success) {
            $pins[$definition.Name] = $match.Groups[1].Value
        } else {
            $pins[$definition.Name] = $null
        }
    }
    return $pins
}

# Resolves a FetchContent download directory. CMake materialises those under
# <build>/_deps/<lowercase-name>-src.
function Get-PacFetchedSourceDir {
    param(
        [Parameter(Mandatory = $true)] [string] $BuildDir,
        [Parameter(Mandatory = $true)] [string] $DependencyName
    )

    $depsRoot = Join-Path $BuildDir '_deps'
    if (-not (Test-Path -LiteralPath $depsRoot -PathType Container)) { return $null }
    $candidates = @(Get-ChildItem -LiteralPath $depsRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like ('*-src') })
    foreach ($candidate in $candidates) {
        if ($candidate.Name -like ('*' + $DependencyName + '*')) { return $candidate.FullName }
    }
    return $null
}

# Records the dependency revisions the build actually resolved: the cached
# SOURCE_DIR entries when the workspace supplies them, otherwise the
# FetchContent download directory. A dependency that cannot be resolved, or
# whose revision differs from the pin, is not a qualified input.
function Get-PacBuildDependencyIdentity {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)] [string] $RepoRoot,
        [Parameter(Mandatory = $true)] [string] $BuildDir
    )

    $pins = Get-PacDependencyPin -CMakeListsPath (Join-Path $RepoRoot 'CMakeLists.txt')
    $definitions = @(
        @{ Name = 'PhlosionEngine'; CacheVariable = 'PHLOSION_ENGINE_SOURCE_DIR' },
        @{ Name = 'PhlosionVFX'; CacheVariable = 'PHLOSION_VFX_SOURCE_DIR' },
        @{ Name = 'PhlosionPackages'; CacheVariable = 'PHLOSION_PACKAGES_SOURCE_DIR' }
    )

    $entries = New-Object 'System.Collections.Generic.List[object]'
    foreach ($definition in $definitions) {
        $pin = $pins[$definition.Name]
        if ($definition.Name -eq 'PhlosionPackages' -and
            (Get-PacCMakeCacheValue -BuildDir $BuildDir -Name 'PAC_BUILD_EDITOR') -eq 'OFF') {
            $entries.Add([pscustomobject][ordered]@{
                name = $definition.Name
                pin = $pin
                resolved_root = $null
                resolved_head = $null
                resolution = 'not-selected'
                dirty_state = $null
                ok = $true
                problem = $null
            })
            continue
        }
        $cached = Get-PacCMakeCacheValue -BuildDir $BuildDir -Name $definition.CacheVariable
        $resolvedRoot = $null
        $resolution = 'unresolved'
        if (-not [string]::IsNullOrWhiteSpace($cached) -and (Test-Path -LiteralPath $cached -PathType Container)) {
            $resolvedRoot = [IO.Path]::GetFullPath($cached)
            $resolution = 'cmake-cache'
        } else {
            $fetched = Get-PacFetchedSourceDir -BuildDir $BuildDir -DependencyName $definition.Name
            if ($null -ne $fetched) {
                $resolvedRoot = [IO.Path]::GetFullPath($fetched)
                $resolution = 'fetchcontent'
            }
        }

        $resolvedHead = $null
        if ($null -ne $resolvedRoot) {
            $resolvedHead = Get-PacGitRevision -RepositoryRoot $resolvedRoot
        }

        $problem = $null
        if ($null -eq $pin) {
            $problem = ('no pinned commit was found in CMakeLists.txt')
        } elseif ($null -eq $resolvedRoot) {
            $problem = ('the build did not resolve a checkout for this dependency')
        } elseif ($null -eq $resolvedHead) {
            $problem = ('the resolved checkout has no readable git revision')
        } elseif ($resolvedHead -ne $pin) {
            $problem = ('resolved revision {0} does not match pin {1}' -f $resolvedHead, $pin)
        }

        $entries.Add([pscustomobject][ordered]@{
            name = $definition.Name
            pin = $pin
            resolved_root = $resolvedRoot
            resolved_head = $resolvedHead
            resolution = $resolution
            dirty_state = $(if ($null -ne $resolvedRoot) { Get-PacGitDirtyState -RepositoryRoot $resolvedRoot } else { $null })
            ok = ($null -eq $problem)
            problem = $problem
        })
    }

    $problems = @($entries.ToArray() | Where-Object { -not $_.ok } | ForEach-Object {
        ('{0}: {1}' -f $_.name, $_.problem)
    })

    return [pscustomobject][ordered]@{
        schema = $script:DependencyIdentitySchema
        build_dir = $BuildDir
        dev_root = (Get-PacCMakeCacheValue -BuildDir $BuildDir -Name 'PHLOSION_DEV_ROOT')
        entries = @($entries.ToArray())
        ok = ($problems.Count -eq 0)
        problems = @($problems)
    }
}

function Assert-PacBuildDependencyIdentity {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)] $Identity)

    if ($Identity.ok) { return $Identity }
    $message = 'The build resolved dependency revisions that do not match the pinned commits, ' +
        'so a qualification run must not report a mismatched or unidentified binary as qualified.' +
        [Environment]::NewLine +
        ($Identity.problems -join [Environment]::NewLine)
    throw $message
}

function Get-PacBuildExecutablePath {
    param(
        [Parameter(Mandatory = $true)] [string] $BuildDir,
        [Parameter(Mandatory = $true)] [string] $Config,
        [Parameter(Mandatory = $true)] [string] $Name
    )

    $candidates = @(
        (Join-Path $BuildDir (Join-Path $Config ($Name + '.exe'))),
        (Join-Path $BuildDir ($Name + '.exe'))
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }
    return $null
}

function Get-PacStrictCookedEnvironmentVariable {
    return [ordered]@{
        PHLOSION_REQUIRE_COOKED_ASSETS = [string]$script:StrictCookedEnvironment['PHLOSION_REQUIRE_COOKED_ASSETS']
        PHLOSION_TRACE_ASSET_LOADS = [string]$script:StrictCookedEnvironment['PHLOSION_TRACE_ASSET_LOADS']
    }
}

# Sets the supplied process environment variables for the duration of the
# script block and restores the previous values even when it throws.
function Invoke-PacWithEnvironment {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)] $Variables,
        [Parameter(Mandatory = $true)] [scriptblock] $Script
    )

    $backup = [ordered]@{}
    foreach ($name in @($Variables.Keys)) {
        $backup[[string]$name] = [Environment]::GetEnvironmentVariable([string]$name, 'Process')
        [Environment]::SetEnvironmentVariable([string]$name, [string]$Variables[$name], 'Process')
    }
    try {
        return (& $Script)
    } finally {
        foreach ($name in @($backup.Keys)) {
            $previous = $backup[$name]
            # Windows PowerShell removes a process variable when it is set to
            # null, while PowerShell 7 stores an empty string. An empty or null
            # backup value therefore both mean "was not set" and must be
            # removed through the Env: provider so the restore is host
            # independent.
            if ([string]::IsNullOrEmpty($previous)) {
                Remove-Item -LiteralPath ('Env:' + [string]$name) -ErrorAction SilentlyContinue
            } else {
                [Environment]::SetEnvironmentVariable([string]$name, [string]$previous, 'Process')
            }
        }
    }
}

Export-ModuleMember -Function @(
    'Resolve-PacQualificationPath',
    'Get-PacGitCommandResult',
    'Get-PacGitRevision',
    'Get-PacGitDirtyState',
    'Get-PacCMakeCacheValue',
    'Get-PacDependencyPin',
    'Get-PacFetchedSourceDir',
    'Get-PacBuildDependencyIdentity',
    'Assert-PacBuildDependencyIdentity',
    'Get-PacBuildExecutablePath',
    'Get-PacStrictCookedEnvironmentVariable',
    'Invoke-PacWithEnvironment'
)
