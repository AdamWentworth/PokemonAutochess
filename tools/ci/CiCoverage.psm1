# Coverage, scope and runtime-fallback helpers shared by the hosted CI
# workflow, tools/run_ctest_ci.ps1, tools/full_check.ps1 and
# tools/qualify_content.ps1.
#
# Windows PowerShell 5.1 compatible: no ConvertFrom-Json -AsHashtable, no
# multi-child Join-Path, no null-coalescing operators.

Set-StrictMode -Version Latest

$script:CoverageManifestSchema = 'pac-ci-coverage-manifest-v1'
$script:ValidCoverageModes = @('source', 'content')

# Each marker maps to a diagnostic a real code path emits when a renderer or
# asset fallback was taken:
#   [Render][ModelCache] Unable to render model
#       -> src/game/runtime/session/SessionBackendAssetBridge.cpp
#   required PHLO prefab is missing / native .phmodel assets are offline inputs
#       -> src/game/runtime/render_model_cache/RenderModelCache.cpp
#   fallback window init failed -> src/game/runtime/GameRunner.cpp
#   combat tuning fallback / type chart fallback
#       -> src/game/systems/CombatSystem.cpp
#   [Init] Failed to load -> src/game/runtime/GameBootstrap.cpp
#   pokeball.glb unavailable
#       -> src/game/runtime/shared/capture/SharedCaptureModelBridge.cpp
#   Adapter enumeration unavailable
#       -> src/game/runtime/renderer/RendererStartupDiagnostics.cpp
#   PHLO dependency hash mismatch / typed resource has no DATA chunk
#       -> src/game/runtime/phlosion/PhlosionModelObject.cpp
# They are phrases rather than the bare word "fallback" so contracts that
# intentionally exercise fallback behaviour do not trip them.
$script:RuntimeFallbackMarkers = @(
    '[Render][ModelCache] Unable to render model',
    'required PHLO prefab is missing',
    'native .phmodel assets are offline inputs and require a cooked PHLO prefab',
    'fallback window init failed',
    'combat tuning fallback',
    'type chart fallback',
    '[Init] Failed to load',
    'pokeball.glb unavailable',
    'Adapter enumeration unavailable',
    'PHLO dependency hash mismatch',
    'Phlosion typed resource has no DATA chunk'
)

function Get-CiPropertyValue {
    param(
        [AllowNull()] $InputObject,
        [Parameter(Mandatory = $true)] [string] $Name
    )

    if ($null -eq $InputObject) { return $null }
    $property = $InputObject.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Get-CiRuntimeFallbackMarker {
    param(
        [AllowEmptyCollection()] [string[]] $Lines = @(),
        [string[]] $Markers = $script:RuntimeFallbackMarkers
    )

    $found = New-Object 'System.Collections.Generic.List[string]'
    foreach ($line in @($Lines)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        foreach ($marker in $Markers) {
            if ($line.IndexOf($marker, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                $found.Add(("{0} :: {1}" -f $marker, $line.Trim()))
            }
        }
    }
    return @($found.ToArray())
}

function Assert-CiRuntimeOutput {
    param(
        [AllowEmptyCollection()] [string[]] $Lines = @(),
        [string[]] $Markers = $script:RuntimeFallbackMarkers,
        [string] $Context = 'runtime output'
    )

    $found = @(Get-CiRuntimeFallbackMarker -Lines $Lines -Markers $Markers)
    if ($found.Count -gt 0) {
        throw ("{0} reported render or asset fallback failures:`n{1}" -f $Context, ($found -join "`n"))
    }
}

# The coverage manifest declares private-content contracts by their PAC test
# name (for example `content_invariants`); CTest registers those as
# `PAC_Tests.content_invariants`. `PAC_Tools.*` contracts are registered under
# their declared name.
function ConvertTo-CiCtestTestName {
    param([Parameter(Mandatory = $true)] [string] $Name)

    if ($Name -match '^PAC_[A-Za-z0-9_]+\.') { return $Name }
    return ("PAC_Tests.{0}" -f $Name)
}

# Deduplicated union of every declared private-content contract. A contract may
# legitimately belong to more than one suite, so the union - not the sum - is
# the declared private-content scope.
function Get-CiDeclaredPrivateTestNames {
    param([Parameter(Mandatory = $true)] $Manifest)

    $names = New-Object 'System.Collections.Generic.List[string]'
    foreach ($suite in @(Get-CiPropertyValue $Manifest 'suites')) {
        foreach ($test in @(Get-CiPropertyValue $suite 'tests')) {
            $name = [string]$test
            if ([string]::IsNullOrWhiteSpace($name)) { continue }
            if (-not $names.Contains($name)) { $names.Add($name) }
        }
    }
    return @($names.ToArray())
}

function Assert-CiCoverageManifest {
    param(
        [Parameter(Mandatory = $true)] $Manifest,
        [string] $Path = '<coverage-manifest>'
    )

    $schema = [string](Get-CiPropertyValue $Manifest 'schema')
    if ($schema -ne $script:CoverageManifestSchema) {
        throw ("Coverage manifest '{0}' declares schema '{1}'; expected '{2}'." -f $Path, $schema, $script:CoverageManifestSchema)
    }

    $mode = [string](Get-CiPropertyValue $Manifest 'mode')
    if ($script:ValidCoverageModes -notcontains $mode) {
        throw ("Coverage manifest '{0}' declares mode '{1}'; expected one of: {2}." -f $Path, $mode, ($script:ValidCoverageModes -join ', '))
    }

    $counts = Get-CiPropertyValue $Manifest 'counts'
    if ($null -eq $counts) {
        throw ("Coverage manifest '{0}' has no counts block." -f $Path)
    }
    $registered = [int](Get-CiPropertyValue $counts 'registered_contract_tests')
    if ($registered -le 0) {
        throw ("Coverage manifest '{0}' reports zero registered contract tests; a configuration that selects no tests fails." -f $Path)
    }
    $privateCount = [int](Get-CiPropertyValue $counts 'private_content_tests')
    $excludedCount = [int](Get-CiPropertyValue $counts 'excluded_private_tests')
    $suiteCount = [int](Get-CiPropertyValue $counts 'suites')

    $suites = @(Get-CiPropertyValue $Manifest 'suites')
    if ($suiteCount -le 0 -or $suites.Count -ne $suiteCount) {
        throw ("Coverage manifest '{0}' declares {1} suites but enumerates {2}." -f $Path, $suiteCount, $suites.Count)
    }
    foreach ($suite in $suites) {
        $name = [string](Get-CiPropertyValue $suite 'name')
        if ([string]::IsNullOrWhiteSpace($name)) {
            throw ("Coverage manifest '{0}' has a suite entry without a name." -f $Path)
        }
        $status = [string](Get-CiPropertyValue $suite 'status')
        $prerequisites = @(Get-CiPropertyValue $suite 'prerequisites')
        $tests = @(Get-CiPropertyValue $suite 'tests')
        if ($prerequisites.Count -eq 0) {
            throw ("Private content suite '{0}' declares no prerequisites; prerequisites must reflect catalog, source and cooked requirements." -f $name)
        }
        if ($tests.Count -eq 0) {
            throw ("Private content suite '{0}' declares no tests." -f $name)
        }
        foreach ($test in $tests) {
            if ([string]::IsNullOrWhiteSpace([string]$test)) {
                throw ("Private content suite '{0}' declares a test without a name." -f $name)
            }
        }
        if ($status -ne 'enabled' -and $status -ne 'excluded') {
            throw ("Private content suite '{0}' declares status '{1}'; expected 'enabled' or 'excluded'." -f $name, $status)
        }
        if ($mode -eq 'content' -and $status -ne 'enabled') {
            throw ("Content mode requires suite '{0}' to be enabled, but the manifest reports '{1}'." -f $name, $status)
        }
        if ($mode -eq 'source' -and $status -ne 'excluded') {
            throw ("Source mode requires suite '{0}' to be excluded and reported, but the manifest reports '{1}'." -f $name, $status)
        }
    }

    # The declared private-content count must be the deduplicated union of the
    # suite test lists, so a manifest can never claim a scope it does not
    # enumerate. Counting alone would let two suites name one contract while the
    # counts still agreed.
    $declaredNames = @(Get-CiDeclaredPrivateTestNames -Manifest $Manifest)
    if ($declaredNames.Count -ne $privateCount) {
        throw ("Coverage manifest '{0}' declares {1} private content tests but its suites enumerate {2} distinct names." -f $Path, $privateCount, $declaredNames.Count)
    }

    $excludedTests = @(Get-CiPropertyValue $Manifest 'excluded_tests')
    if ($mode -eq 'source') {
        if ($privateCount -le 0) {
            throw ("Coverage manifest '{0}' is in source mode and declares no private content tests to exclude." -f $Path)
        }
        if ($excludedCount -ne $privateCount) {
            throw ("Coverage manifest '{0}' reports {1} excluded private tests but declares {2} private content tests." -f $Path, $excludedCount, $privateCount)
        }
        if ($excludedTests.Count -ne $excludedCount) {
            throw ("Coverage manifest '{0}' reports {1} excluded private tests but enumerates {2}." -f $Path, $excludedCount, $excludedTests.Count)
        }
        $excludedNames = New-Object 'System.Collections.Generic.List[string]'
        foreach ($entry in $excludedTests) {
            $name = [string](Get-CiPropertyValue $entry 'name')
            $reason = [string](Get-CiPropertyValue $entry 'reason')
            $requires = @(Get-CiPropertyValue $entry 'requires_suites')
            if ([string]::IsNullOrWhiteSpace($name) -or [string]::IsNullOrWhiteSpace($reason) -or $requires.Count -eq 0) {
                throw ("Coverage manifest '{0}' has an excluded test without a name, required suite or reason." -f $Path)
            }
            if ($excludedNames.Contains($name)) {
                throw ("Coverage manifest '{0}' enumerates excluded test '{1}' more than once." -f $Path, $name)
            }
            $excludedNames.Add($name)
        }
        $undeclared = @($excludedNames | Where-Object { -not ($declaredNames -contains $_) })
        $unreported = @($declaredNames | Where-Object { -not ($excludedNames.Contains($_)) })
        if ($undeclared.Count -gt 0 -or $unreported.Count -gt 0) {
            throw (
                "Coverage manifest '{0}' source scope must exclude exactly the declared private content tests; unreported: [{1}]; not declared by any suite: [{2}]." -f
                    $Path, ($unreported -join ', '), ($undeclared -join ', '))
        }
    } else {
        if ($privateCount -le 0) {
            throw ("Coverage manifest '{0}' is in content mode but declares no private content tests." -f $Path)
        }
        if ($excludedCount -ne 0 -or $excludedTests.Count -ne 0) {
            throw ("Coverage manifest '{0}' is in content mode but still excludes {1} private tests; content mode must not shrink coverage." -f $Path, $excludedCount)
        }
    }
}

function Read-CiCoverageManifest {
    param([Parameter(Mandatory = $true)] [string] $Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw ("CI coverage manifest not found: {0}. Reconfigure the build with CMake so CI can report the selected scope." -f $Path)
    }
    $text = Get-Content -LiteralPath $Path -Raw
    try {
        $manifest = ConvertFrom-Json -InputObject $text
    } catch {
        throw ("CI coverage manifest is not valid JSON: {0} ({1})" -f $Path, $_.Exception.Message)
    }
    Assert-CiCoverageManifest -Manifest $manifest -Path $Path
    return $manifest
}

function Get-CiCoverageSummaryLines {
    param([Parameter(Mandatory = $true)] $Manifest)

    $counts = Get-CiPropertyValue $Manifest 'counts'
    $lines = New-Object 'System.Collections.Generic.List[string]'
    $lines.Add((
        'CI coverage: mode={0} registered={1} privateContent={2} excludedPrivate={3} suites={4}' -f
            [string](Get-CiPropertyValue $Manifest 'mode'),
            [int](Get-CiPropertyValue $counts 'registered_contract_tests'),
            [int](Get-CiPropertyValue $counts 'private_content_tests'),
            [int](Get-CiPropertyValue $counts 'excluded_private_tests'),
            [int](Get-CiPropertyValue $counts 'suites')))
    foreach ($suite in @(Get-CiPropertyValue $Manifest 'suites')) {
        $prerequisites = @(Get-CiPropertyValue $suite 'prerequisites')
        $tests = @(Get-CiPropertyValue $suite 'tests')
        $missing = @(Get-CiPropertyValue $suite 'missing')
        $lines.Add((
            '  suite {0}: {1} ({2} tests, {3} prerequisites, {4} missing)' -f
                [string](Get-CiPropertyValue $suite 'name'),
                [string](Get-CiPropertyValue $suite 'status'),
                $tests.Count,
                $prerequisites.Count,
                $missing.Count))
    }
    foreach ($entry in @(Get-CiPropertyValue $Manifest 'excluded_tests')) {
        $requires = @(Get-CiPropertyValue $entry 'requires_suites')
        $lines.Add((
            '  excluded {0}: {1} (requires: {2})' -f
                [string](Get-CiPropertyValue $entry 'name'),
                [string](Get-CiPropertyValue $entry 'reason'),
                ($requires -join ', ')))
    }
    return @($lines.ToArray())
}

function Get-CtestRegisteredTestNames {
    param(
        [Parameter(Mandatory = $true)] [string] $BuildDir,
        [string] $Config = 'Debug',
        [string] $Label = ''
    )

    $ctestArguments = @('--test-dir', $BuildDir, '-C', $Config, '-N')
    if (-not [string]::IsNullOrWhiteSpace($Label)) {
        $ctestArguments += @('-L', $Label)
    }
    $output = @(& ctest @ctestArguments 2>&1)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        $text = (@($output | ForEach-Object { [string]$_ }) -join "`n")
        throw ("ctest -N failed with exit code {0}:`n{1}" -f $exitCode, $text)
    }
    $names = New-Object 'System.Collections.Generic.List[string]'
    foreach ($line in @($output | ForEach-Object { [string]$_ })) {
        $match = [regex]::Match($line, '^\s*Test\s+#\d+:\s*(.+?)\s*$')
        if ($match.Success) { $names.Add($match.Groups[1].Value) }
    }
    return @($names.ToArray())
}

function Get-CtestRegisteredTestCount {
    param(
        [Parameter(Mandatory = $true)] [string] $BuildDir,
        [string] $Config = 'Debug',
        [string] $Label = ''
    )

    $ctestArguments = @('--test-dir', $BuildDir, '-C', $Config, '-N')
    if (-not [string]::IsNullOrWhiteSpace($Label)) {
        $ctestArguments += @('-L', $Label)
    }
    $output = @(& ctest @ctestArguments 2>&1)
    $exitCode = $LASTEXITCODE
    $text = (@($output | ForEach-Object { [string]$_ }) -join "`n")
    if ($exitCode -ne 0) {
        throw ("ctest -N failed with exit code {0}:`n{1}" -f $exitCode, $text)
    }
    $match = [regex]::Match($text, 'Total Tests:\s*(\d+)')
    if (-not $match.Success) { return 0 }
    return [int]$match.Groups[1].Value
}

# Asserts that the configuration the build directory actually registered matches
# the requested scope and the manifest declaration. Counts alone would let a
# shrunken or renamed selection pass.
function Assert-CiCtestScope {
    param(
        [Parameter(Mandatory = $true)] [string] $BuildDir,
        [string] $Config = 'Debug',
        [Parameter(Mandatory = $true)] $Manifest,
        [string] $ExpectedMode = ''
    )

    $mode = [string](Get-CiPropertyValue $Manifest 'mode')
    if (-not [string]::IsNullOrWhiteSpace($ExpectedMode) -and $mode -ne $ExpectedMode) {
        throw (
            "The configured scope is '{0}' but '{1}' was requested. Reconfigure the build with -DPAC_ENABLE_PRIVATE_ASSET_TESTS=ON for content qualification or OFF for the hosted source suite." -f
                $mode, $ExpectedMode)
    }

    $actualNames = @(Get-CtestRegisteredTestNames -BuildDir $BuildDir -Config $Config -Label 'private-content')
    if ($mode -eq 'content') {
        $expectedNames = @(Get-CiDeclaredPrivateTestNames -Manifest $Manifest | ForEach-Object { ConvertTo-CiCtestTestName $_ })
        $missing = @($expectedNames | Where-Object { $actualNames -notcontains $_ })
        $unexpected = @($actualNames | Where-Object { $expectedNames -notcontains $_ })
        if ($missing.Count -gt 0 -or $unexpected.Count -gt 0) {
            throw (
                "CTest registered a different private-content selection than the coverage manifest declares; not registered: [{0}]; not declared: [{1}]." -f
                    ($missing -join ', '), ($unexpected -join ', '))
        }
    } elseif ($actualNames.Count -ne 0) {
        throw (
            "The source scope must register zero private-content tests, but CTest registered {0}: {1}. Reconfigure with -DPAC_ENABLE_PRIVATE_ASSET_TESTS=OFF." -f
                $actualNames.Count, ($actualNames -join ', '))
    }

    return $mode
}

Export-ModuleMember -Function @(
    'Get-CiPropertyValue',
    'Get-CiRuntimeFallbackMarker',
    'Assert-CiRuntimeOutput',
    'ConvertTo-CiCtestTestName',
    'Get-CiDeclaredPrivateTestNames',
    'Assert-CiCoverageManifest',
    'Read-CiCoverageManifest',
    'Get-CiCoverageSummaryLines',
    'Get-CtestRegisteredTestNames',
    'Get-CtestRegisteredTestCount',
    'Assert-CiCtestScope'
)
