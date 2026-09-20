# Contract tests for the qualification report support module.
#
# The fixture creates tiny real git repositories and a synthetic CMake cache so
# dependency resolution, dirty-state recording and environment scoping are
# exercised as behaviour rather than as text matching.

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot 'ci/QualificationSupport.psm1') -Force

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ("pac-qualification-support-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null

$checks = 0
function Assert-True {
    param([bool]$Condition, [string]$Message)
    $script:checks += 1
    if (-not $Condition) { throw $Message }
}
function Assert-Throws {
    param([scriptblock]$Action, [string]$Pattern, [string]$Message)
    $script:checks += 1
    $threw = $false
    try {
        & $Action | Out-Null
    } catch {
        $threw = $true
        if ($_.Exception.Message -notmatch $Pattern) {
            throw ("{0} (threw: {1})" -f $Message, $_.Exception.Message)
        }
    }
    if (-not $threw) { throw ("{0} (no exception was raised)" -f $Message) }
}
function New-FixtureRepository {
    param([string]$Name)
    $path = Join-Path $fixtureRoot $Name
    New-Item -ItemType Directory -Path $path -Force | Out-Null
    & git -C $path init -q
    if ($LASTEXITCODE -ne 0) { throw "git init failed for the fixture repository '$Name'." }
    & git -C $path -c user.email=fixture@example.com -c user.name=fixture -c commit.gpgsign=false commit -q --allow-empty -m ('fixture-' + $Name)
    if ($LASTEXITCODE -ne 0) { throw "git commit failed for the fixture repository '$Name'." }
    return (Get-PacGitRevision -RepositoryRoot $path)
}
function Write-FixtureCMakeLists {
    param([string]$EnginePin, [string]$VfxPin, [string]$PackagesPin)
    $lines = @(
        'cmake_minimum_required(VERSION 3.22)',
        'set(PHLOSION_ENGINE_GIT_TAG',
        ('    "' + $EnginePin + '")'),
        'set(PHLOSION_VFX_GIT_TAG',
        ('    "' + $VfxPin + '")'),
        'set(PHLOSION_PACKAGES_GIT_TAG',
        ('    "' + $PackagesPin + '")'),
        ''
    )
    [IO.File]::WriteAllText((Join-Path $fixtureRoot 'CMakeLists.txt'), ($lines -join [Environment]::NewLine))
}
function Write-FixtureCache {
    param([string]$BuildDir, [string]$EngineRoot, [string]$VfxRoot, [string]$PackagesRoot)
    $lines = @(
        'CMAKE_GENERATOR:INTERNAL=Visual Studio 18 2026',
        ('PHLOSION_DEV_ROOT:PATH=' + $fixtureRoot),
        ('PHLOSION_ENGINE_SOURCE_DIR:PATH=' + $EngineRoot),
        ('PHLOSION_VFX_SOURCE_DIR:PATH=' + $VfxRoot),
        ('PHLOSION_PACKAGES_SOURCE_DIR:PATH=' + $PackagesRoot),
        'PAC_BUILD_EDITOR:BOOL=ON',
        ''
    )
    New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $BuildDir 'CMakeCache.txt'), ($lines -join [Environment]::NewLine))
}

try {
    $engineHead = New-FixtureRepository -Name 'engine'
    $vfxHead = New-FixtureRepository -Name 'vfx'
    $packagesHead = New-FixtureRepository -Name 'packages'
    Assert-True ($null -ne $engineHead) 'The fixture repositories must report a git revision.'
    Assert-True ($engineHead -ne $vfxHead) 'The fixture repositories must have distinct revisions.'

    $buildDir = Join-Path $fixtureRoot 'build'
    $engineRoot = Join-Path $fixtureRoot 'engine'
    $vfxRoot = Join-Path $fixtureRoot 'vfx'
    $packagesRoot = Join-Path $fixtureRoot 'packages'
    Write-FixtureCache -BuildDir $buildDir -EngineRoot $engineRoot -VfxRoot $vfxRoot -PackagesRoot $packagesRoot
    Write-FixtureCMakeLists -EnginePin $engineHead -VfxPin $vfxHead -PackagesPin $packagesHead

    $matching = Get-PacBuildDependencyIdentity -RepoRoot $fixtureRoot -BuildDir $buildDir
    Assert-True ($matching.ok) ('Pinned revisions resolved from the cache must qualify: ' + ($matching.problems -join '; '))
    Assert-True ($matching.entries.Count -eq 3) 'Every pinned dependency must be recorded.'
    $engineEntry = @($matching.entries | Where-Object { $_.name -eq 'PhlosionEngine' })[0]
    Assert-True ($engineEntry.resolved_head -eq $engineHead) 'The resolved revision must come from the cached checkout path.'
    Assert-True ($engineEntry.resolution -eq 'cmake-cache') 'A cache-provided checkout must be reported as such.'
    Assert-True ($engineEntry.pin -eq $engineHead) 'The recorded pin must come from CMakeLists.txt.'
    $null = Assert-PacBuildDependencyIdentity -Identity $matching

    $cachePath = Join-Path $buildDir 'CMakeCache.txt'
    $editorCache = [IO.File]::ReadAllText($cachePath)
    [IO.File]::WriteAllText($cachePath, $editorCache.Replace('PAC_BUILD_EDITOR:BOOL=ON', 'PAC_BUILD_EDITOR:BOOL=OFF'))
    $withoutEditor = Get-PacBuildDependencyIdentity -RepoRoot $fixtureRoot -BuildDir $buildDir
    $unusedPackage = @($withoutEditor.entries | Where-Object { $_.name -eq 'PhlosionPackages' })[0]
    Assert-True ($withoutEditor.ok) 'An editor-disabled build must qualify without resolving editor packages.'
    Assert-True ($unusedPackage.resolution -eq 'not-selected') 'Editor packages must be explicitly recorded as unused.'
    Assert-True ($null -eq $unusedPackage.resolved_head) 'A stale package cache entry must not be reported as a build input.'
    Assert-True ($engineEntry.dirty_state.state -eq 'clean') 'Actual dependency dirty state must accompany its revision.'
    [IO.File]::WriteAllText($cachePath, $editorCache)

    Write-FixtureCMakeLists -EnginePin ('0' * 40) -VfxPin $vfxHead -PackagesPin $packagesHead
    $mismatched = Get-PacBuildDependencyIdentity -RepoRoot $fixtureRoot -BuildDir $buildDir
    Assert-True (-not $mismatched.ok) 'A checkout that differs from the pin must not qualify.'
    Assert-True (($mismatched.problems -join ' ') -match 'PhlosionEngine') 'The mismatch must name the dependency.'
    Assert-True (($mismatched.problems -join ' ') -match 'does not match pin') 'The mismatch must explain the pin difference.'
    $mismatchIdentity = $mismatched
    $assertMismatch = { Assert-PacBuildDependencyIdentity -Identity $mismatchIdentity }
    Assert-Throws -Action $assertMismatch -Pattern 'must not report' -Message 'An assertion over a mismatched identity must fail.'

    $emptyBuild = Join-Path $fixtureRoot 'build-fetched'
    New-Item -ItemType Directory -Path $emptyBuild -Force | Out-Null
    $unresolved = Get-PacBuildDependencyIdentity -RepoRoot $fixtureRoot -BuildDir $emptyBuild
    Assert-True (-not $unresolved.ok) 'An unresolved dependency must not qualify.'
    Assert-True (($unresolved.problems -join ' ') -match 'did not resolve') 'The unresolved case must explain the missing checkout.'
    $unresolvedEntries = @($unresolved.entries | Where-Object { $_.resolution -eq 'unresolved' })
    Assert-True ($unresolvedEntries.Count -eq 3) 'Unresolved entries must be recorded as such.'

    $dirtyClean = Get-PacGitDirtyState -RepositoryRoot $engineRoot
    Assert-True (-not $dirtyClean.dirty) 'A clean fixture must report clean.'
    Assert-True ($dirtyClean.state -eq 'clean') 'The clean state must be reported by name.'
    [IO.File]::WriteAllText((Join-Path $engineRoot 'untracked.txt'), 'x')
    $dirty = Get-PacGitDirtyState -RepositoryRoot $engineRoot
    Assert-True ($dirty.dirty) 'An untracked file must mark the qualification dirty.'
    Assert-True ($dirty.state -eq 'dirty') 'The dirty state must be reported by name.'
    Assert-True (($dirty.changed_paths -join ' ') -match 'untracked.txt') 'The changed paths must be recorded.'
    Remove-Item -LiteralPath (Join-Path $engineRoot 'untracked.txt') -Force

    $expectedRelative = [IO.Path]::GetFullPath((Join-Path $fixtureRoot 'debug/ci'))
    $resolvedRelative = Resolve-PacQualificationPath -RepoRoot $fixtureRoot -PathValue 'debug/ci'
    Assert-True ($resolvedRelative -eq $expectedRelative) 'A relative path must resolve under the repository root.'
    $absolute = [IO.Path]::GetFullPath($fixtureRoot)
    $resolvedAbsolute = Resolve-PacQualificationPath -RepoRoot 'C:/elsewhere' -PathValue $absolute
    Assert-True ($resolvedAbsolute -eq $absolute) 'A rooted path must be preserved.'

    [IO.File]::WriteAllText((Join-Path $buildDir 'CMakeCache.txt'), ('PAC_ENABLE_PRIVATE_ASSET_TESTS:BOOL=ON' + [Environment]::NewLine))
    Assert-True ((Get-PacCMakeCacheValue -BuildDir $buildDir -Name 'PAC_ENABLE_PRIVATE_ASSET_TESTS') -eq 'ON') 'Cached CMake values must be readable.'
    Assert-True ($null -eq (Get-PacCMakeCacheValue -BuildDir $buildDir -Name 'ABSENT')) 'A missing cache entry must be null.'

    $execDir = Join-Path $buildDir 'Debug'
    New-Item -ItemType Directory -Path $execDir -Force | Out-Null
    $forgeStub = Join-Path $execDir 'PhlosionForge.exe'
    [IO.File]::WriteAllText($forgeStub, 'stub')
    $resolvedForge = Get-PacBuildExecutablePath -BuildDir $buildDir -Config 'Debug' -Name 'PhlosionForge'
    Assert-True ($resolvedForge -eq $forgeStub) 'A config-scoped build executable must resolve from the build directory.'
    Assert-True ($null -eq (Get-PacBuildExecutablePath -BuildDir $buildDir -Config 'Release' -Name 'PhlosionForge')) 'A missing executable must resolve to null.'

    $strict = Get-PacStrictCookedEnvironmentVariable
    Assert-True ($strict['PHLOSION_REQUIRE_COOKED_ASSETS'] -eq '1') 'Strict cooked mode must be part of the scoped environment.'
    Assert-True ($strict['PHLOSION_TRACE_ASSET_LOADS'] -eq '1') 'Asset-load tracing must be part of the scoped environment.'

    [Environment]::SetEnvironmentVariable('PHLOSION_REQUIRE_COOKED_ASSETS', 'pre-existing', 'Process')
    [Environment]::SetEnvironmentVariable('PHLOSION_TRACE_ASSET_LOADS', $null, 'Process')
    $observed = Invoke-PacWithEnvironment -Variables $strict -Script {
        [pscustomobject][ordered]@{
            require = [Environment]::GetEnvironmentVariable('PHLOSION_REQUIRE_COOKED_ASSETS', 'Process')
            trace = [Environment]::GetEnvironmentVariable('PHLOSION_TRACE_ASSET_LOADS', 'Process')
        }
    }
    Assert-True ($observed.require -eq '1') 'Strict cooked mode must be set for the scoped child.'
    Assert-True ($observed.trace -eq '1') 'Asset-load tracing must be set for the scoped child.'
    Assert-True ([Environment]::GetEnvironmentVariable('PHLOSION_REQUIRE_COOKED_ASSETS', 'Process') -eq 'pre-existing') 'The previous value must be restored.'
    Assert-True ($null -eq [Environment]::GetEnvironmentVariable('PHLOSION_TRACE_ASSET_LOADS', 'Process')) 'A previously unset variable must be removed again.'

    $throwing = { Invoke-PacWithEnvironment -Variables $strict -Script { throw 'fixture failure' } }
    Assert-Throws -Action $throwing -Pattern 'fixture failure' -Message 'A failing scoped child must keep its own error.'
    Assert-True ([Environment]::GetEnvironmentVariable('PHLOSION_REQUIRE_COOKED_ASSETS', 'Process') -eq 'pre-existing') 'The environment must be restored when the child throws.'
    Assert-True ($null -eq [Environment]::GetEnvironmentVariable('PHLOSION_TRACE_ASSET_LOADS', 'Process')) 'The unset trace variable must not leak.'

    Write-Host ("[CiQualificationSupportContractTest] PASS ({0} checks)" -f $checks)
} finally {
    [Environment]::SetEnvironmentVariable('PHLOSION_REQUIRE_COOKED_ASSETS', $null, 'Process')
    [Environment]::SetEnvironmentVariable('PHLOSION_TRACE_ASSET_LOADS', $null, 'Process')
    if (Test-Path -LiteralPath $fixtureRoot) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
