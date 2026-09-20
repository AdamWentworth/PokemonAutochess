# Contract tests for the CI coverage manifest helpers.
#
# These exercise real manifest parsing and validation rather than mirroring the
# implementation: each case writes (or omits) a small synthetic manifest and
# asserts that scope mistakes are rejected. The declared private-content scope is
# the deduplicated union of the suite test lists, and the source scope has to
# exclude exactly those names - a manifest may not agree on a count while naming
# a different contract.
#
# The zero-test and selection-mismatch behaviour of CTest itself is proven against
# the real wrapper in tools/test_ci_ctest_wrapper.ps1, which runs ctest on small
# synthetic test trees instead of text-matching this module.

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "ci/CiCoverage.psm1") -Force

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ("pac-ci-coverage-" + [Guid]::NewGuid().ToString("N"))
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

# Three distinct contracts, one of which is shared by a second suite. The
# declared private-content scope is the three-name union, not the four suite
# memberships.
function New-SourceManifest {
    return [pscustomobject][ordered]@{
        schema = "pac-ci-coverage-manifest-v1"
        mode = "source"
        private_content_root = "C:/repo"
        counts = [pscustomobject][ordered]@{
            registered_contract_tests = 221
            private_content_tests = 3
            excluded_private_tests = 3
            suites = 2
        }
        suites = @(
            [pscustomobject][ordered]@{
                name = "native-model-ir"
                status = "excluded"
                prerequisites = @("assets/models/0001_Bulbasaur_SV.phmodel")
                missing = @("assets/models/0001_Bulbasaur_SV.phmodel")
                tests = @("model_parse_smoke", "content_invariants")
            },
            [pscustomobject][ordered]@{
                name = "cooked-route1-content"
                status = "excluded"
                prerequisites = @("content/phlosion/cook_manifest.json")
                missing = @("content/phlosion/cook_manifest.json")
                tests = @("content_invariants", "route1_arena_pilot_contract")
            }
        )
        excluded_tests = @(
            [pscustomobject][ordered]@{
                name = "model_parse_smoke"
                requires_suites = @("native-model-ir")
                reason = "PAC_ENABLE_PRIVATE_ASSET_TESTS is OFF (explicit source suite)."
            },
            [pscustomobject][ordered]@{
                name = "content_invariants"
                requires_suites = @("native-model-ir", "cooked-route1-content")
                reason = "PAC_ENABLE_PRIVATE_ASSET_TESTS is OFF (explicit source suite)."
            },
            [pscustomobject][ordered]@{
                name = "route1_arena_pilot_contract"
                requires_suites = @("cooked-route1-content")
                reason = "PAC_ENABLE_PRIVATE_ASSET_TESTS is OFF (explicit source suite)."
            }
        )
    }
}

function New-ContentManifest {
    return [pscustomobject][ordered]@{
        schema = "pac-ci-coverage-manifest-v1"
        mode = "content"
        private_content_root = "C:/repo"
        counts = [pscustomobject][ordered]@{
            registered_contract_tests = 254
            private_content_tests = 3
            excluded_private_tests = 0
            suites = 1
        }
        suites = @(
            [pscustomobject][ordered]@{
                name = "native-model-ir"
                status = "enabled"
                prerequisites = @("assets/models/0001_Bulbasaur_SV.phmodel")
                missing = @()
                tests = @("model_parse_smoke", "content_invariants", "model_asset_smoke")
            }
        )
        excluded_tests = @()
    }
}

function Write-FixtureManifest {
    param([string]$Name, $Manifest)
    $path = Join-Path $fixtureRoot $Name
    ($Manifest | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $path -Encoding UTF8
    return $path
}

try {
    $sourcePath = Write-FixtureManifest -Name "source.json" -Manifest (New-SourceManifest)
    $source = Read-CiCoverageManifest -Path $sourcePath
    Assert-True ($source.mode -eq "source") "A valid source manifest must load."
    $summary = @(Get-CiCoverageSummaryLines -Manifest $source)
    Assert-True ($summary.Count -ge 3) "A coverage summary must report counts, suites and exclusions."
    Assert-True (($summary -join "`n") -match "mode=source") "The summary must state the configured scope."
    Assert-True (($summary -join "`n") -match "excluded model_parse_smoke") "The summary must enumerate omitted contracts."

    $contentPath = Write-FixtureManifest -Name "content.json" -Manifest (New-ContentManifest)
    $content = Read-CiCoverageManifest -Path $contentPath
    Assert-True ($content.mode -eq "content") "A valid content manifest must load."

    Assert-Throws -Action { Read-CiCoverageManifest -Path (Join-Path $fixtureRoot "absent.json") } `
        -Pattern "not found" -Message "A missing coverage manifest must fail."

    $invalidJsonPath = Join-Path $fixtureRoot "invalid.json"
    Set-Content -LiteralPath $invalidJsonPath -Value "{ not json" -Encoding UTF8
    Assert-Throws -Action { Read-CiCoverageManifest -Path $invalidJsonPath } `
        -Pattern "not valid JSON" -Message "Unparseable coverage manifests must fail."

    $zero = New-SourceManifest
    $zero.counts.registered_contract_tests = 0
    Assert-Throws -Action { Assert-CiCoverageManifest -Manifest $zero -Path "zero.json" } `
        -Pattern "zero registered" -Message "A configuration selecting no tests must fail."

    $shrunken = New-SourceManifest
    $shrunken.counts.excluded_private_tests = 2
    Assert-Throws -Action { Assert-CiCoverageManifest -Manifest $shrunken -Path "shrunken.json" } `
        -Pattern "excluded private tests" -Message "Source mode must exclude every declared private contract."

    # Equal counts, wrong names: the scope must be the same set of contracts.
    $wrongNames = New-SourceManifest
    $wrongNames.excluded_tests[0].name = "not_a_declared_contract"
    Assert-Throws -Action { Assert-CiCoverageManifest -Manifest $wrongNames -Path "wrong-names.json" } `
        -Pattern "exclude exactly" -Message "Source exclusions must match the declared contracts by name, not only by count."

    # The declared count must be the deduplicated union of the suite test lists.
    $duplicateAcrossSuites = New-SourceManifest
    $duplicateAcrossSuites.suites[1].tests = @("content_invariants")
    Assert-Throws -Action { Assert-CiCoverageManifest -Manifest $duplicateAcrossSuites -Path "duplicate.json" } `
        -Pattern "distinct names" -Message "A manifest may not count suite memberships instead of distinct contracts."

    $contentShrunk = New-ContentManifest
    $contentShrunk.counts.excluded_private_tests = 2
    $contentShrunk.excluded_tests = @(
        [pscustomobject][ordered]@{
            name = "model_parse_smoke"
            requires_suites = @("native-model-ir")
            reason = "missing corpus"
        }
    )
    Assert-Throws -Action { Assert-CiCoverageManifest -Manifest $contentShrunk -Path "content-shrunk.json" } `
        -Pattern "must not shrink coverage" -Message "Content mode must reject any omitted private contract."

    $contentExcludedSuite = New-ContentManifest
    $contentExcludedSuite.suites[0].status = "excluded"
    Assert-Throws -Action { Assert-CiCoverageManifest -Manifest $contentExcludedSuite -Path "content-suite.json" } `
        -Pattern "requires suite" -Message "Content mode must reject an excluded suite."

    $noPrereq = New-ContentManifest
    $noPrereq.suites[0].prerequisites = @()
    Assert-Throws -Action { Assert-CiCoverageManifest -Manifest $noPrereq -Path "no-prereq.json" } `
        -Pattern "no prerequisites" -Message "Suites must declare prerequisites."

    $suiteMismatch = New-ContentManifest
    $suiteMismatch.counts.suites = 4
    Assert-Throws -Action { Assert-CiCoverageManifest -Manifest $suiteMismatch -Path "suite-mismatch.json" } `
        -Pattern "declares 4 suites" -Message "The suite count must match the enumerated suites."

    $badSchema = New-ContentManifest
    $badSchema.schema = "pac-ci-coverage-manifest-v2"
    Assert-Throws -Action { Assert-CiCoverageManifest -Manifest $badSchema -Path "bad-schema.json" } `
        -Pattern "declares schema" -Message "Unknown manifest schemas must fail."

    $cleanOutput = @("Test #1: PAC_Tests.model_parse_smoke", "100% tests passed, 0 tests failed out of 1")
    Assert-CiRuntimeOutput -Lines $cleanOutput -Context "fixture"
    Assert-True $true "Clean runtime output must not be rejected."

    $fallbackOutput = @(
        "[Init] Failed to load content/phlosion/scenes/route1.phscene",
        "combat tuning fallback engaged"
    )
    Assert-Throws -Action { Assert-CiRuntimeOutput -Lines $fallbackOutput -Context "fixture" } `
        -Pattern "render or asset fallback" -Message "Runtime fallback diagnostics must be rejected."

    # The markers must cover the diagnostics that actually appear when cooked
    # content is missing, not just the generic word "fallback".
    $realFailures = @(
        "[Render][ModelCache] Unable to render model assets/models/0004_Charmander_SV.phmodel",
        "required PHLO prefab is missing for assets/models/0016_Pidgey_SV.phmodel: content/phlosion/objects/0016_Pidgey/0016_Pidgey.phlo",
        "native .phmodel assets are offline inputs and require a cooked PHLO prefab for assets/models/0001_Bulbasaur_SV.phmodel"
    )
    $detected = @(Get-CiRuntimeFallbackMarker -Lines $realFailures)
    Assert-True ($detected.Count -eq $realFailures.Count) `
        ("Every observed render/asset failure diagnostic must be rejected (detected {0} of {1})." -f $detected.Count, $realFailures.Count)

    Assert-Throws -Action { Assert-CiCtestScope -BuildDir $fixtureRoot -Config Debug -Manifest $content -ExpectedMode "source" } `
        -Pattern "was requested" -Message "A scope mismatch between the configured and requested suite must fail."

    Write-Host ("[CiCoverageContractTest] PASS ({0} checks)" -f $checks)
} finally {
    if (Test-Path -LiteralPath $fixtureRoot) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
